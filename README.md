# Quiz Game: Cities and Countries

This repository contains the implementation of a multiplayer quiz game focused on geography, programming, chemistry, music genres, athletics, mobile phone brands, Nobel laureates, and more. The game is built with a client-server architecture, allowing multiple players to connect, participate in rounds of trivia questions, and compete for the highest score.

## Features

- **Multiplayer Support**: Allows multiple players to connect simultaneously and compete in real-time.
- **Dynamic Configuration**: Game settings and question-answer database loaded from a configuration file (`config.ini`).
- **Real-time Scoring and Ranking**: Players receive points based on correctness, response uniqueness, and response time.
- **GUI Client**: User-friendly graphical client implemented with Python's Tkinter.

## Project Structure

- **Server** (`server.c`): Implements the core server functionality including managing client connections, game state, question handling, and scoring.
- **Client** (`client.py`): GUI-based client allowing players to connect to the server, respond to questions, and view real-time updates.
- **Configuration File** (`config.ini`): Contains game settings and a comprehensive set of trivia questions and answers.

## Getting Started

### Prerequisites

- **Server**: Requires a Unix-like operating system with GCC installed.
- **Client**: Requires Python 3.x with Tkinter installed.

### Installation

1. Compile the server:

```bash
gcc server.c -o quiz-server
```

2. Run the server:

```bash
./quiz-server
```

3. Launch the client:

```bash
python3 client.py [server_ip]
```
- If no IP address is specified, it defaults to `127.0.0.1` (localhost).

## Gameplay

- Players provide their nickname upon joining.
- Each round presents a trivia question loaded from `config.ini`.
- Players answer questions within a time limit specified in the config file.
- Points are awarded based on answer correctness, uniqueness, and response speed.

## Customizing Questions

To add or modify questions and answers, edit the `config.ini` file:

```ini
[QUESTION]
Your Question Here
[ANSWER]
Correct Answer 1
Correct Answer 2
...
```

Developed by Bartłomiej Rudowicz and Paweł Kierkosz.
