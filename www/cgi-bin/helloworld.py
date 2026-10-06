#!/usr/bin/python3
# CGI mínimo: lee QUERY_STRING y responde HTML.
# Un CGI escribe cabeceras, una línea en blanco y después el cuerpo.
import os
from urllib.parse import parse_qs

params = parse_qs(os.environ.get("QUERY_STRING", ""))
nombre = params.get("nombre", ["mundo"])[0]

print("Content-Type: text/html")
print()
print(f"<h1>Hola, {nombre}!</h1>")
print(f"<p>Método: {os.environ.get('REQUEST_METHOD')}</p>")
