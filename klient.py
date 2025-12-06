import socket
import tkinter as tk
from tkinter import ttk
from tkinter import scrolledtext
from tkinter import messagebox


def send_request():
    host = entry_host.get().strip()
    port_text = entry_port.get().strip()
    path = entry_path.get().strip()
    method = combo_method.get().strip().upper()
    body = text_body.get("1.0", tk.END)

    if not host:
        messagebox.showerror("Błąd", "Podaj host (np. localhost).")
        return

    if not port_text.isdigit():
        messagebox.showerror("Błąd", "Port musi być liczbą (np. 8080).")
        return

    port = int(port_text)
    if port <= 0 or port > 65535:
        messagebox.showerror("Błąd", "Port musi być w zakresie 1–65535.")
        return

    if not path:
        path = "/"
    if not path.startswith("/"):
        path = "/" + path

    method = method or "GET"

    # Przygotujemy body i nagłówki.
    body_bytes = b""
    if method == "PUT":
        body_bytes = body.encode("utf-8")
        content_length = len(body_bytes)
    else:
        # Dla prostoty nie wysyłamy body przy innych metodach.
        content_length = 0

    # Składamy żądanie HTTP/1.1.
    # Minimalnie: request line, Host, Connection.
    # Przy PUT: Content-Length.
    request_lines = []
    request_lines.append(f"{method} {path} HTTP/1.1")
    request_lines.append(f"Host: {host}:{port}")
    request_lines.append("Connection: close")

    if method == "PUT":
        request_lines.append(f"Content-Length: {content_length}")
        # Opcjonalnie:
        request_lines.append("Content-Type: text/plain; charset=utf-8")

    # Pusta linia oddzielająca nagłówki od body.
    request_str = "\r\n".join(request_lines) + "\r\n\r\n"
    request_bytes = request_str.encode("utf-8") + body_bytes

    # Wyczyść pole z odpowiedzią.
    text_response.delete("1.0", tk.END)

    try:
        # Tworzymy połączenie TCP.
        with socket.create_connection((host, port), timeout=5) as sock:
            sock.sendall(request_bytes)

            # Czytamy odpowiedź aż serwer zamknie połączenie.
            chunks = []
            while True:
                data = sock.recv(4096)
                if not data:
                    break
                chunks.append(data)

        response_bytes = b"".join(chunks)
        try:
            response_text = response_bytes.decode("utf-8", errors="replace")
        except Exception:
            response_text = repr(response_bytes)

        text_response.insert(tk.END, response_text)

    except Exception as e:
        messagebox.showerror("Błąd połączenia", str(e))


# Tworzymy okno główne.
root = tk.Tk()
root.title("Prosty klient HTTP (GET/HEAD/PUT/DELETE)")

# Ramka z parametrami połączenia.
frame_conn = ttk.LabelFrame(root, text="Połączenie")
frame_conn.grid(row=0, column=0, sticky="ew", padx=10, pady=5)

ttk.Label(frame_conn, text="Host:").grid(row=0, column=0, sticky="w", padx=5, pady=2)
entry_host = ttk.Entry(frame_conn, width=20)
entry_host.grid(row=0, column=1, sticky="w", padx=5, pady=2)
entry_host.insert(0, "localhost")

ttk.Label(frame_conn, text="Port:").grid(row=0, column=2, sticky="w", padx=5, pady=2)
entry_port = ttk.Entry(frame_conn, width=6)
entry_port.grid(row=0, column=3, sticky="w", padx=5, pady=2)
entry_port.insert(0, "8080")

ttk.Label(frame_conn, text="Ścieżka:").grid(row=1, column=0, sticky="w", padx=5, pady=2)
entry_path = ttk.Entry(frame_conn, width=30)
entry_path.grid(row=1, column=1, columnspan=3, sticky="ew", padx=5, pady=2)
entry_path.insert(0, "/")

ttk.Label(frame_conn, text="Metoda:").grid(row=0, column=4, sticky="w", padx=5, pady=2)
combo_method = ttk.Combobox(frame_conn, values=["GET", "HEAD", "PUT", "DELETE"], width=8)
combo_method.grid(row=0, column=5, sticky="w", padx=5, pady=2)
combo_method.set("GET")

# Ramka na body (dla PUT).
frame_body = ttk.LabelFrame(root, text="Treść żądania (body, używane głównie przy PUT)")
frame_body.grid(row=1, column=0, sticky="nsew", padx=10, pady=5)

text_body = scrolledtext.ScrolledText(frame_body, wrap=tk.WORD, width=80, height=8)
text_body.grid(row=0, column=0, padx=5, pady=5)

# Przycisk "Wyślij".
button_send = ttk.Button(root, text="Wyślij żądanie", command=send_request)
button_send.grid(row=2, column=0, pady=5)

# Ramka na odpowiedź.
frame_resp = ttk.LabelFrame(root, text="Odpowiedź serwera (nagłówki + body)")
frame_resp.grid(row=3, column=0, sticky="nsew", padx=10, pady=5)

text_response = scrolledtext.ScrolledText(frame_resp, wrap=tk.WORD, width=80, height=20)
text_response.grid(row=0, column=0, padx=5, pady=5)

# Rozciąganie okna.
root.rowconfigure(1, weight=1)
root.rowconfigure(3, weight=3)
root.columnconfigure(0, weight=1)

root.mainloop()
