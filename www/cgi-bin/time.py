#!/usr/bin/python3
# Index de /cgi-bin: muestra la hora del servidor.
import datetime

print("Content-Type: text/html")
print()
print(f"<h1>Hora del servidor: {datetime.datetime.now():%H:%M:%S}</h1>")
