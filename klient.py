import socket
import threading
import tkinter as tk
from tkinter import messagebox, simpledialog
from queue import Queue
import sys

# Pobranie adresu IP z parametrów wywołania (można podać IP serwera w argumencie)
if len(sys.argv) > 1:
    SERVER_HOST = sys.argv[1]
else:
    SERVER_HOST = "127.0.0.1"

SERVER_PORT = 12345

nickname_set = False
client_socket = None
message_queue = Queue()

time_left = 0  

def connect_to_server():
    # Funkcja odpowiedzialna za łączenie z serwerem.
    # Ustawia timeout na 600s, włącza keepalive dla socketu,
    # a po udanym połączeniu przywraca domyślny brak timeoutu.
    # Obsługuje również ewentualne błędy (timeout, firewall itp.).

    global client_socket
    try:
        # Włączamy timeout przy connect
        socket.setdefaulttimeout(600.0)

        client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        # Włączamy keepalive, żeby szybciej wykrywać zrywanie łącza
        client_socket.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)

        client_socket.connect((SERVER_HOST, SERVER_PORT))
        print("Połączono z serwerem!")
    except socket.timeout:
        print("Przekroczono czas oczekiwania na połączenie (timeout).")
        messagebox.showerror("Błąd", "Timeout przy łączeniu z serwerem. Spróbuj ponownie.")
        return
    except Exception as e:
        print(f"Błąd połączenia z serwerem: {e}")
        messagebox.showerror("Błąd", f"Nie udało się połączyć z serwerem: {e}")
        return
    finally:
        # Przywracamy domyślny brak timeoutu
        socket.setdefaulttimeout(None)

def listen_to_server():
    # Funkcja - wątek nasłuchujący.
    # W pętli odbiera dane z serwera i wrzuca je do kolejki message_queue.
    # Jeżeli socket.recv() zwróci 0, to znaczy że serwer się rozłączył.
    # Obsługuje też ewentualne wyjątki (np. błąd gniazda).
    try:
        while True:
            data = client_socket.recv(1024)
            if not data:
                break
            message_queue.put(data.decode("utf-8", errors="ignore"))
    except Exception as e:
        message_queue.put(f"Błąd w wątku nasłuchującym: {e}")

def process_queue():
    # Funkcja do okresowego sprawdzania kolejki message_queue
    # i przetwarzania odebranych linii od serwera.
    # W zależności od treści linii - zmieniamy stan GUI (np. message_box, ranking_box).
    global nickname_set, time_left
    while not message_queue.empty():
        full_message = message_queue.get()
        lines = full_message.split("\n")
        for line in lines:
            line = line.strip()
            if not line:
                continue

            # Obsługa "Nowa gra startuje!" -> wyczyszczenie GUI
            if line == "Nowa gra rozpoczęta!":
                # Czyścimy pola
                message_box.config(state=tk.NORMAL)
                message_box.delete("1.0", tk.END)
                message_box.config(state=tk.DISABLED)

                ranking_box.config(state=tk.NORMAL)
                ranking_box.delete("1.0", tk.END)
                ranking_box.config(state=tk.DISABLED)

                question_label.config(text="Oczekiwanie na pytanie...")
                time_label.config(text="Pozostały czas: --")
                continue

            # Obsługa "Podaj swój pseudonim"
            if line.startswith("Podaj swój pseudonim"):
                if not nickname_set:
                    ask_for_nickname()
                else:
                    # Jeżeli już mamy nickname, a serwer znowu prosi o nick,
                    # to wyświetlamy błąd w okienku
                    message_box.config(state=tk.NORMAL)
                    message_box.insert(tk.END, "[Błąd?] Serwer ponownie pyta o pseudonim.\n")
                    message_box.config(state=tk.DISABLED)
                    message_box.see(tk.END)

            elif line.startswith("Pseudonim zajęty"):
                # Serwer informuje, że pseudonim jest zajęty, więc czyścimy message_box,
                # wypisujemy komunikat i ponownie prosimy użytkownika o inny nick
                message_box.config(state=tk.NORMAL)
                message_box.delete("1.0", tk.END)
                message_box.config(state=tk.DISABLED)
                message_box.config(state=tk.NORMAL)
                message_box.insert(tk.END, line + "\n", "centered")
                message_box.config(state=tk.DISABLED)
                message_box.see(tk.END)
                nickname_set = False
                ask_for_nickname()

            elif line.startswith("Pytanie:"):
                # Nowe pytanie z serwera -> czyścimy pole odpowiedzi i włączamy je
                answer_entry.delete(0, tk.END)
                question_label.config(text=line)
                answer_entry.config(state=tk.NORMAL)
                send_button.config(state=tk.NORMAL)

            elif line.startswith("TIME_LEFT="):
                # Serwer przekazuje czas, który pozostał
                parts = line.split("=")
                if len(parts) == 2:
                    try:
                        time_left = int(parts[1])
                    except ValueError:
                        pass

            elif line.startswith("IN_GAME=0"):
                # Runda zakończona / gracz nie może odpowiadać -> czyścimy pole i blokujemy
                answer_entry.delete(0, tk.END)
                answer_entry.config(state=tk.DISABLED)
                send_button.config(state=tk.DISABLED)

            elif line.startswith("IN_GAME=1"):
                # Runda w toku / gracz może odpowiadać
                answer_entry.config(state=tk.NORMAL)
                send_button.config(state=tk.NORMAL)

            elif ("Ranking" in line or "wyniki" in line or "Punkty za pytanie" in line):
                # Komunikaty dot. rankingu/wyników -> wyświetlamy w ranking_box
                ranking_box.config(state=tk.NORMAL)
                ranking_box.insert(tk.END, line + "\n")
                ranking_box.config(state=tk.DISABLED)
                ranking_box.see(tk.END)

            else:
                # Obsługa pozostałych komunikatów
                if "Zalogowano pomyślnie!" in line:
                    # Komunikat serwera, że login się udał
                    message_box.config(state=tk.NORMAL)
                    message_box.delete("1.0", tk.END)
                    message_box.config(state=tk.DISABLED)
                    nickname_set = True
                    message_box.config(state=tk.NORMAL)
                    message_box.insert(tk.END, line + "\n", "centered")
                    message_box.config(state=tk.DISABLED)
                    message_box.see(tk.END)
                else:
                    # Wszelkie inne linie -> pokazujemy w message_box
                    message_box.config(state=tk.NORMAL)
                    message_box.delete("1.0", tk.END)
                    message_box.config(state=tk.DISABLED)
                    message_box.config(state=tk.NORMAL)
                    message_box.insert(tk.END, line + "\n")
                    message_box.config(state=tk.DISABLED)
                    message_box.see(tk.END)

    # Ponawiamy sprawdzanie co 100ms
    root.after(100, process_queue)

def ask_for_nickname():
    # Pyta w okienku dialogowym o pseudonim,
    # a następnie wysyła go do serwera.
    nickname = simpledialog.askstring("Pseudonim", "Podaj swój pseudonim:")
    if not nickname:
        messagebox.showerror("Błąd", "Musisz podać pseudonim, aby dołączyć do gry!")
        return
    client_socket.sendall((nickname + "\n").encode("utf-8"))

def send_message():
    # Wywoływana po kliknięciu "Wyślij odpowiedź".
    # Sprawdza, czy pole nie jest puste, wysyła do serwera.
    # Czyści entry i blokuje ponownie możliwość wpisywania
    # (tak, by tylko raz odpowiedzieć).
    message = answer_entry.get()
    if not message.strip():
        messagebox.showwarning("Błąd", "Nie możesz wysłać pustej odpowiedzi!")
        return
    try:
        client_socket.sendall((message + "\n").encode("utf-8"))
        answer_entry.delete(0, tk.END)
        answer_entry.config(state=tk.DISABLED)
        send_button.config(state=tk.DISABLED)
    except Exception as e:
        messagebox.showerror("Błąd", f"Nie udało się wysłać wiadomości: {e}")

# --- GUI ---
root = tk.Tk()
root.title("Quiz - Państwa-Miasta")

# message_box -> okno, w którym wyświetlamy komunikaty (np. pseudonim zajęty itp.)
message_box = tk.Text(root, height=3, width=45, state=tk.DISABLED, font=("Arial", 12))
message_box.pack(pady=5)

message_box.tag_configure("centered", justify="center")

question_label = tk.Label(root, text="Oczekiwanie na pytanie...", font=("Arial", 14))
question_label.pack(pady=5)

time_label = tk.Label(root, text="Pozostały czas: --", font=("Arial", 14))
time_label.pack(pady=5)

answer_entry = tk.Entry(root, font=("Arial", 14), width=70, state=tk.DISABLED)
answer_entry.pack(pady=5)

send_button = tk.Button(root, text="Wyślij odpowiedź", font=("Arial", 14), state=tk.DISABLED, command=send_message)
send_button.pack(pady=5)

ranking_box = tk.Text(root, height=15, width=70, state=tk.DISABLED, font=("Arial", 12))
ranking_box.pack(pady=5)

connect_to_server()

# Tworzymy wątek nasłuchujący
listener_thread = threading.Thread(target=listen_to_server, daemon=True)
listener_thread.start()

def update_timer():
    # Co sekundę zmniejszamy time_left i aktualizujemy etykietę time_label.
    # Kiedy spadnie do 0, wyświetlamy 0s.
    global time_left
    if time_left > 0:
        time_left -= 1
        time_label.config(text=f"Pozostały czas: {time_left}s")
    else:
        time_label.config(text="Pozostały czas: 0s")
    root.after(1000, update_timer)

# Uruchamiamy cykliczne sprawdzanie kolejki i timer
root.after(100, process_queue)
root.after(1000, update_timer)
root.mainloop()
