# Quiz Game: "Państwa-Miasta" (States & Cities)

A multiplayer quiz game implemented in C (server) and Python with Tkinter (client). Players compete in real-time rounds, answering category-based questions within a time limit. The game logic includes round handling, answer validation, scoring, and dynamic rankings.

## 🛠 Features

- Multiplayer support via TCP sockets (epoll-based server)
- Graphical client built with Python and Tkinter
- Configurable game settings and questions via `config.ini`
- Real-time scoring and ranking system
- Case-insensitive and uniqueness-based answer validation
- Automatic game restart after rounds are finished

## 📂 Project Structure
├── server.c # Server-side logic (C) 
├── client.py # GUI Client (Python + Tkinter) 
├── config.ini # Game configuration and questions/answers 
└── README.md # You are here

## ⚙️ Configuration (`config.ini`)

The config file defines:

- `TIME_LIMIT`: Time limit for each round (in seconds)
- `MAX_ROUNDS`: Number of rounds per game
- A set of `[QUESTION]` / `[ANSWER]` sections:
  - Each `[QUESTION]` is followed by a single question line.
  - Then one or more `[ANSWER]` sections provide valid answers.

Example:
```ini
TIME_LIMIT=30
MAX_ROUNDS=10

[QUESTION]
Name a European country
[ANSWER]
Germany
France
Italy
...

[QUESTION]
Name a citrus fruit
[ANSWER]
Orange
Lemon
...

gcc server.c -o server
./server
