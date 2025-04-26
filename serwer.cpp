#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>

#define PORT 12345
#define BUFFER_SIZE 1024

// Zmienne globalne: limit czasu na rundę i liczba rund (wczytywane z config.ini)
static int g_time_limit;
static int g_max_rounds;

// Ustawienia dot. pytań i odpowiedzi
#define MAX_QUESTIONS 50
static int  g_loaded_questions = 0;   // Ile pytań wczytano
static char questionsConfig[MAX_QUESTIONS][BUFFER_SIZE]; // Tablica pytań
static char ***answersDB = NULL;      // Tablica 3-wymiarowa: answersDB[i][j] - j-ta poprawna odpowiedź do pytania i
static int  *answerCounts = NULL;     // Ile odpowiedzi przypada na pytanie i

// --- Funkcje wczytywania configu (plik config.ini) ---

int load_config(const char *filename, int *time_limit, int *max_rounds) {
    // Wczytywanie TIME_LIMIT i MAX_ROUNDS z pliku
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Nie można otworzyć pliku konfiguracyjnego");
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') {
            continue; // Pomijamy komentarze i puste linie
        }
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *value_str = eq + 1;

        // Odczyt klucz=wartość
        if (strcmp(key, "TIME_LIMIT") == 0) {
            *time_limit = atoi(value_str);
        } else if (strcmp(key, "MAX_ROUNDS") == 0) {
            *max_rounds = atoi(value_str);
        }
    }

    fclose(fp);
    return 0;
}

// Zwolnienie pamięci dynamicznie zaalokowanej
void free_resources() {
    // Usuwamy tablice z pytaniami i odpowiedziami
    for (int i = 0; i < g_loaded_questions; i++) {
        for (int j = 0; j < answerCounts[i]; j++) {
            free(answersDB[i][j]);
        }
        free(answersDB[i]);
    }
    free(answersDB);
    free(answerCounts);
}

// Wczytywanie bazy pytań/odpowiedzi z pliku config.ini
int load_answers_from_config(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Brak pliku %s lub nie można wczytać.\n", filename);
        return -1;
    }

    answersDB = (char ***)calloc(MAX_QUESTIONS, sizeof(char**));
    if (!answersDB) {
        fprintf(stderr, "Błąd alokacji pamięci dla answersDB.\n");
        fclose(fp);
        return -1;
    }
    answerCounts = (int *)calloc(MAX_QUESTIONS, sizeof(int));
    if (!answerCounts) {
        fprintf(stderr, "Błąd alokacji pamięci dla answerCounts.\n");
        free(answersDB);
        fclose(fp);
        return -1;
    }

    int currentQ = -1;
    char line[BUFFER_SIZE];

    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        // Sekcja [QUESTION]
        if (strcmp(line, "[QUESTION]") == 0) {
            if (fgets(line, sizeof(line), fp)) {
                nl = strchr(line, '\n');
                if (nl) *nl = '\0';
                currentQ++;
                if (currentQ >= MAX_QUESTIONS) {
                    fprintf(stderr, "Za dużo pytań w pliku konfiguracyjnym!\n");
                    break;
                }
                strcpy(questionsConfig[currentQ], line);
                answersDB[currentQ] = NULL;
                answerCounts[currentQ] = 0;
                g_loaded_questions++;
            }
        }
        // Sekcja [ANSWER]
        else if (strcmp(line, "[ANSWER]") == 0) {
            while (fgets(line, sizeof(line), fp)) {
                char *nl2 = strchr(line, '\n');
                if (nl2) *nl2 = '\0';

                if (strcmp(line, "[QUESTION]") == 0 || strcmp(line, "[ANSWER]") == 0) {
                    // Cofamy wskaźnik w pliku, by nie zgubić tej linii
                    fseek(fp, -((long)strlen(line) + 1), SEEK_CUR);
                    break;
                }
                int idx = answerCounts[currentQ];
                char **newAnswers = (char **)realloc(answersDB[currentQ], (idx + 1) * sizeof(char *));
                if (!newAnswers) {
                    fprintf(stderr, "Błąd alokacji pamięci dla odpowiedzi.\n");
                    fclose(fp);
                    free_resources();
                    return -1;
                }
                answersDB[currentQ] = newAnswers;
                answersDB[currentQ][idx] = strdup(line);
                if (!answersDB[currentQ][idx]) {
                    fprintf(stderr, "Błąd alokacji pamięci dla odpowiedzi.\n");
                    fclose(fp);
                    free_resources();
                    return -1;
                }
                answerCounts[currentQ]++;
            }
        }
    }

    fclose(fp);
    return 0;
}

// Funkcja porównująca stringi case-insensitive
int strcase_compare(const char *a, const char *b) {
    while (*a && *b) {
        char ca = tolower((unsigned char)*a);
        char cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return (int)(unsigned char)ca - (int)(unsigned char)cb;
        }
        a++;
        b++;
    }
    return (int)(unsigned char)tolower((unsigned char)*a)
         - (int)(unsigned char)tolower((unsigned char)*b);
}

// Sprawdza, czy 'response' istnieje w answersDB dla rundy 'roundIndex'
int is_in_database(int roundIndex, const char *response) {
    if (roundIndex < 0 || roundIndex >= g_loaded_questions) {
        return 0;
    }
    for (int i = 0; i < answerCounts[roundIndex]; i++) {
        if (strcase_compare(answersDB[roundIndex][i], response) == 0) {
            return 1;
        }
    }
    return 0;
}

// Struktura gracza
typedef struct Player {
    int fd;             // deskryptor gniazda
    char *name;         // pseudonim
    char *response;     // odpowiedź (jeden string na rundę)
    int score;          // suma punktów
    int answered;       // czy odpowiedział w tej rundzie (flaga)
    int in_game;        // czy jest w grze w tej rundzie
    int got_name;       // czy w ogóle ma pseudonim
    int lastPoints;     // punkty uzyskane w ostatniej rundzie
    double answerTime;  // czas odpowiedzi (sekundy od startu rundy)
    struct Player *next;
} Player;

// Lista graczy (powiązania przez next), plus ogólne liczniki
static Player *playersHead = NULL;
int active_players = 0;

// Zmienne stanu rund
int current_round = 0;
int round_in_progress = 0;
char current_question[BUFFER_SIZE] = {0};
time_t round_start_time;

// Zmienne do czekania 20s na start gry i do wyświetlania rankingu końcowego
int waiting_for_first_player = 0;
time_t first_player_wait_start = 0;

int showing_final_ranking = 0;
time_t final_ranking_start = 0;

// Zamyka wszystkie gniazda (wywoływane przy wyjściu z programu)
void close_all_sockets() {
    Player *p = playersHead;
    while (p) {
        close(p->fd);
        p = p->next;
    }
}

// Ustawia deskryptor w tryb nieblokujący
static void set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Dodanie nowego gracza do listy
static void add_player(int fd) {
    Player *p = (Player*) malloc(sizeof(Player));
    p->fd = fd;
    p->name = NULL;
    p->response = NULL;
    p->score = 0;
    p->answered = 0;
    p->in_game = 0;
    p->got_name = 0;
    p->lastPoints = 0;
    p->answerTime = -1.0;
    p->next = playersHead;
    playersHead = p;
    active_players++;
}

// Usunięcie gracza (rozłączył się itp.)
static void remove_player(int fd) {
    Player *curr = playersHead;
    Player *prev = NULL;

    while (curr) {
        if (curr->fd == fd) {
            if (!prev) {
                playersHead = curr->next;
            } else {
                prev->next = curr->next;
            }
            close(curr->fd);
            if (curr->name) free(curr->name);
            if (curr->response) free(curr->response);
            free(curr);
            active_players--;

            // Jeżeli już nie ma graczy -> reset stanu
            if (active_players == 0) {
                current_round = 0;
                round_in_progress = 0;
                memset(current_question, 0, sizeof(current_question));
                fprintf(stderr, "INFO: Wszyscy gracze wyszli - gra zostaje zresetowana.\n");
            }
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

// Wyszukiwanie gracza po deskryptorze gniazda
static Player* find_player_by_fd(int fd) {
    Player *p = playersHead;
    while (p) {
        if (p->fd == fd) return p;
        p = p->next;
    }
    return NULL;
}

// Sprawdza, czy pseudonim jest już zajęty
int is_name_taken(const char *name) {
    Player *p = playersHead;
    while (p) {
        if (p->name && strcmp(p->name, name) == 0) {
            return 1;
        }
        p = p->next;
    }
    return 0;
}

// Wysyłanie tekstu do wszystkich graczy
void send_to_all(const char *message) {
    if (!message || !*message) return;
    Player *p = playersHead;
    while (p) {
        send(p->fd, message, strlen(message), 0);
        p = p->next;
    }
}

// Struktura pomocnicza do sortowania rankingu
typedef struct {
    Player *player;
    int position;
} RankedPlayer;

// Porównanie wg score (malejąco)
int compare_scores(const void *a, const void *b) {
    const RankedPlayer *ra = (const RankedPlayer *)a;
    const RankedPlayer *rb = (const RankedPlayer *)b;
    return rb->player->score - ra->player->score;
}

// Wysyła posortowany ranking do wszystkich
void send_sorted_ranking() {
    int count = 0;
    Player *p = playersHead;
    while (p) {
        count++;
        p = p->next;
    }
    if (count == 0) return;

    RankedPlayer *array = (RankedPlayer*) malloc(sizeof(RankedPlayer) * count);

    int i=0;
    p=playersHead;
    while(p){
        array[i].player=p;
        array[i].position=i+1;
        i++;
        p=p->next;
    }

    qsort(array, count, sizeof(RankedPlayer), compare_scores);

    char msg[BUFFER_SIZE];
    snprintf(msg, BUFFER_SIZE, "Runda %d zakończona, wyniki:\n", current_round + 1);
    send_to_all(msg);

    for(int j=0;j<count;j++){
        Player *pl = array[j].player;
        // Odpowiedź gracza, lastPoints, sumaryczny score itd.
        snprintf(msg, BUFFER_SIZE,
                "%d. %s, Punkty za pytanie: %d, Łącznie: %d, Odpowiedź: %s\n",
                j+1,
                (pl->name ? pl->name : "???"),
                pl->lastPoints,
                pl->score,
                (pl->response ? pl->response : "brak"));
        send_to_all(msg);
    }

    free(array);
}

// Rozpoczęcie rundy (wysłanie pytania, time_left)
void start_round() {
    if (current_round >= g_max_rounds) {
        send_to_all("Gra zakończona!\n");
        return;
    }
    round_in_progress = 1;

    // Każdy gracz wchodzi do gry, p->answered=0 się ustawia w end_round
    Player *p = playersHead;
    while(p){
        p->in_game=1;
        p->answerTime = -1.0;
        p=p->next;
    }

    // Jeżeli mamy załadowane pytania, to bierzemy pytanie current_round
    if (current_round < g_loaded_questions) {
        snprintf(current_question, BUFFER_SIZE, "Pytanie: %.1000s\n", questionsConfig[current_round]);
    }

    send_to_all(current_question);
    round_start_time=time(NULL);

    fprintf(stderr, "DEBUG: Start rundy %d, pytanie = %s\n", current_round+1, current_question);

    char timeMsg[64];
    snprintf(timeMsg, sizeof(timeMsg), "TIME_LEFT=%d\n", g_time_limit);
    send_to_all(timeMsg);
}

// Zakończenie rundy (liczenie punktów, ranking)
void end_round() {
    #define MAX_ANSWERS_TEMP 200
    char *answersTemp[MAX_ANSWERS_TEMP]; // Bufor unikalnych odpowiedzi
    int countTemp[MAX_ANSWERS_TEMP];
    int used=0;

    // Zliczamy i zapamiętujemy unikalne poprawne odpowiedzi
    Player *p=playersHead;
    while (p) {
        if (p->fd>0 && p->in_game==1 && p->response && is_in_database(current_round, p->response)) {
            int found=-1;
            for(int i=0; i<used; i++){
                if(strcase_compare(answersTemp[i], p->response)==0){
                    found=i;
                    break;
                }
            }
            if(found>=0){
                countTemp[found]++;
            } else if(used<MAX_ANSWERS_TEMP){
                answersTemp[used] = strdup(p->response);
                countTemp[used] = 1;
                used++;
            }
        }
        p=p->next;
    }

    // Przydzielamy punkty (m.in. za unikalność i szybkość)
    p=playersHead;
    while (p) {
        int finalPoints = 0;
        if(p->fd>0 && p->in_game==1 && p->response) {
            if(is_in_database(current_round, p->response)) {
                // Odpowiedź w bazie -> sprawdzamy unikalność
                int foundIndex=-1;
                for(int i=0; i<used; i++){
                    if(strcase_compare(answersTemp[i], p->response)==0){
                        foundIndex=i;
                        break;
                    }
                }
                if(foundIndex>=0){
                    int howMany = countTemp[foundIndex];
                    // Jeżeli więcej niż 1 gracz podał taką samą poprawną -> 5pkt, w przeciwnym razie 10
                    if(howMany>1){
                        finalPoints = 5;
                    } else {
                        finalPoints = 10;
                    }

                    // Punkty za szybkość (przedział 0-10)
                    double playerElapsed = p->answerTime;
                    if(playerElapsed<0) playerElapsed = (double)g_time_limit;
                    if(playerElapsed>g_time_limit) playerElapsed = g_time_limit;

                    double delta = (double)g_time_limit / 10.0;
                    double playerTimeLeft = (double)g_time_limit - playerElapsed;
                    int speedPoints=0;
                    // Sprawdzamy progi co 'delta'
                    for(int i=0; i<10; i++){
                        double cutoff = (double)g_time_limit - (i*delta);
                        if(playerTimeLeft >= (cutoff - delta + 0.000001)){
                            speedPoints = 10 - i;
                            break;
                        }
                    }
                    finalPoints += speedPoints;
                }
            }
        }
        p->lastPoints = finalPoints;
        p->score += finalPoints;
        p = p->next;
    }

    // Sprzątamy skopiowane odpowiedzi
    for(int i=0; i<used; i++){
        free(answersTemp[i]);
    }

    // Wysyłamy ranking
    send_sorted_ranking();

    // Blokujemy okienka w kliencie i zerujemy time_left
    send_to_all("IN_GAME=0\n");
    send_to_all("TIME_LEFT=0\n");

    // Reset state'u na kolejną rundę
    p=playersHead;
    while(p){
        if(p->fd>0){
            p->answered=0;
            p->in_game=1;
            if(p->response){
                free(p->response);
                p->response=NULL;
            }
        }
        p=p->next;
    }

    current_round++;
    if(current_round<g_max_rounds){
        start_round();
    } else {
        // Ostatnie pytanie -> koniec gry i czekamy 20s
        send_to_all("Koniec pytań, za 20 sekund ruszy nowa gra / koniec.\n");
        showing_final_ranking = 1;
        final_ranking_start = time(NULL);
        round_in_progress=0;
    }
}

// Obsługa danych od klienta (przyjście pseudonimu lub odpowiedzi)
void handle_client_data(int fd) {
    char buffer[BUFFER_SIZE];
    int read_size=recv(fd, buffer, sizeof(buffer)-1,0);

    if(read_size<=0){
        // Błąd/rozłączenie
        remove_player(fd);
        fprintf(stderr,"DEBUG: Rozłączono gracza fd=%d, active_players=%d\n",fd, active_players);
        return;
    }

    buffer[read_size]='\0';
    // Usuwamy ewentualne '\n'
    if(buffer[read_size-1]=='\n'){
        buffer[read_size-1]='\0';
    }

    Player *p=find_player_by_fd(fd);
    if(!p) return;

    // Jeśli nie ustalono pseudonimu, to wybieramy inny
    if(!p->got_name){
        if(is_name_taken(buffer)){
            const char *taken_msg="Pseudonim zajęty, wybierz inny.\n";
            send(fd, taken_msg, strlen(taken_msg),0);
            return;
        }
        p->name=strdup(buffer);
        p->got_name=1;
        p->score=0;
        p->answered=0;
        p->in_game=0; // poczeka do next rundy
        const char *ok_msg="Zalogowano pomyślnie!\n";
        send(fd, ok_msg, strlen(ok_msg),0);

        fprintf(stderr,"DEBUG: Zalogował się %s(fd=%d), active_players=%d\n",
                p->name, fd, active_players);

        // Jeżeli to pierwszy gracz -> czekamy 20s, żeby inni mogli dołączyć
        if(active_players==1 && current_round<g_max_rounds && !round_in_progress){
            send_to_all("Pierwszy gracz dołączył! Za 20 sekund start rozgrywki...\n");
            waiting_for_first_player=1;
            first_player_wait_start=time(NULL);
        }

        // Jeśli runda w trakcie -> nowy gracz dostaje pytanie + time_left, ale IN_GAME=0
        if(round_in_progress && strlen(current_question)>0){
            send(fd, current_question, strlen(current_question),0);
            time_t now=time(NULL);
            double elapsed=difftime(now, round_start_time);
            int remaining=g_time_limit-(int)elapsed;
            if(remaining<0) remaining=0;
            char msg[64];
            snprintf(msg,sizeof(msg),"TIME_LEFT=%d\n", remaining);
            send(fd,msg,strlen(msg),0);

            const char *block_info="IN_GAME=0\n";
            send(fd, block_info, strlen(block_info),0);
        }
        return;
    }

    // W przeciwnym razie -> to jest odpowiedź gracza
    if(p->answered==0 && p->in_game==1){
        if(p->response) free(p->response);
        p->response=strdup(buffer);
        p->answered=1;
        p->answerTime = difftime(time(NULL), round_start_time);

        fprintf(stderr,"DEBUG: Gracz %s odpowiedział: %s\n", p->name, p->response);
    }
}

int main(){
    // Wczytujemy parametry TIME_LIMIT, MAX_ROUNDS
    if (load_config("config.ini", &g_time_limit, &g_max_rounds) != 0) {
        return 1;
    }

    // Wczytujemy bazę pytań i odpowiedzi
    if (load_answers_from_config("config.ini") != 0) {
        return 1;
    }

    int server_socket;
    struct sockaddr_in server_addr;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket");
        free_resources();
        return 1;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Ustawiamy keepalive (obsługa zerwań łącza w sieciach WAN)
    int keepAlive=1;
    setsockopt(server_socket, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(keepAlive));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        free_resources();
        return 1;
    }
    if (listen(server_socket, 5) < 0) {
        perror("listen");
        close(server_socket);
        free_resources();
        return 1;
    }

    // Tworzymy epoll
    int epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        close(server_socket);
        free_resources();
        return 1;
    }
    set_nonblock(server_socket);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_socket;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_socket, &ev) == -1) {
        perror("epoll_ctl");
        close(server_socket);
        close(epfd);
        free_resources();
        return 1;
    }

    fprintf(stderr, "Serwer działa na porcie %d. Oczekiwanie na graczy...\n", PORT);

    // Pętla główna
    while(1){
        time_t now = time(NULL);

        // Czekamy 20 s na start, jeśli dopiero dołączył pierwszy gracz
        if(waiting_for_first_player==1){
            double diff = difftime(now, first_player_wait_start);
            if(diff >= 20.0){
                waiting_for_first_player=0;
                if(active_players>0 && !round_in_progress && current_round<g_max_rounds){
                    start_round();
                }
            }
        }

        // Jeżeli pokazujemy końcowy ranking przez 20s
        if(showing_final_ranking==1){
            double diff = difftime(now, final_ranking_start);
            if(diff >= 20.0){
                showing_final_ranking=0;
                if(active_players>0){
                    // Reset punktów, start nowej gry
                    Player *tmp = playersHead;
                    while(tmp){
                        tmp->score = 0;
                        tmp->answered = 0;
                        if(tmp->response){
                            free(tmp->response);
                            tmp->response = NULL;
                        }
                        tmp->in_game = 0;
                        tmp = tmp->next;
                    }
                    send_to_all("Nowa gra rozpoczęta!\n");
                    current_round = 0;
                    start_round();
                } else {
                    send_to_all("Gra zakończona! Dzięki za grę!\n");
                }
            }
        }

        // epoll_wait do obsługi nowo przychodzących połączeń/danych (timeout 5000ms)
        struct epoll_event events[64];
        int nfds = epoll_wait(epfd, events, 64, 5000);
        if(nfds==-1){
            perror("epoll_wait");
            break;
        }
        // Obsługa zdarzeń
        for(int i=0;i<nfds;i++){
            if(events[i].data.fd==server_socket){
                // Nowe połączenie
                struct sockaddr_in client_addr;
                socklen_t addr_len=sizeof(client_addr);
                int client_fd=accept(server_socket,(struct sockaddr*)&client_addr,&addr_len);
                if(client_fd==-1){
                    perror("accept");
                    continue;
                }
                set_nonblock(client_fd);

                // Keepalive dla klienta
                int keepC=1;
                setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &keepC, sizeof(keepC));

                struct epoll_event client_ev;
                client_ev.events=EPOLLIN;
                client_ev.data.fd=client_fd;
                if(epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &client_ev)==-1){
                    perror("epoll_ctl");
                    close(client_fd);
                    continue;
                }

                add_player(client_fd);
                const char *ask_name="Podaj swój pseudonim:\n";
                send(client_fd, ask_name, strlen(ask_name),0);

            } else {
                // Dane od istniejącego klienta
                int cfd=events[i].data.fd;
                handle_client_data(cfd);
            }
        }

        // Jeżeli trwa runda -> sprawdzamy warunki jej zakończenia
        if (round_in_progress) {
            now = time(NULL);
            // Upłynął czas?
            if (difftime(now, round_start_time) >= g_time_limit) {
                end_round();
            } else {
                // Czy co najmniej połowa odpowiedziała?
                int total_in_game = 0, answered_count = 0;
                Player *p = playersHead;
                while (p) {
                    if (p->in_game == 1){
                        total_in_game++;
                        if (p->answered == 1){
                            answered_count++;
                        }
                    }
                    p = p->next;
                }

                int required_answers;
                // Zaokrąglamy w górę: (total_in_game+1)/2
                if (total_in_game % 2 == 0) {
                    required_answers = (total_in_game / 2) + 1;
                } else {
                    required_answers = (total_in_game + 1) / 2;
                }

                if (answered_count >= required_answers) {
                    end_round();
                }
            }
        }

        // Jeżeli przekroczyliśmy max_rounds i nie pokazujemy rankingu => koniec
        if(current_round>=g_max_rounds && showing_final_ranking==0){
            break;
        }
    }

    // Sprzątanie - zamykamy wszystkie gniazda, epoll i zasoby
    close_all_sockets();
    close(server_socket);
    close(epfd);
    free_resources();
    return 0;
}
