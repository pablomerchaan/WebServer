#!/usr/bin/python3
# Lee el cuerpo del POST desde stdin (CONTENT_LENGTH bytes) y lo devuelve.
import os
import sys
from html import escape
from urllib.parse import parse_qs

length = int(os.environ.get("CONTENT_LENGTH") or 0)
body = sys.stdin.read(length) if length else ""
campos = parse_qs(body)

print("Content-Type: text/html")
print()
print("<h1>POST recibido por el CGI</h1>")
print(f"<p>Bytes recibidos: {len(body)}</p>")
print("<ul>")
for clave, valores in campos.items():
    print(f"<li>{escape(clave)} = {escape(valores[0])}</li>")
print("</ul>")
