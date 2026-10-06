#!/bin/bash
# CGI en bash: muestra algunas variables de entorno CGI.
echo "Content-Type: text/plain"
echo ""
echo "Hola desde bash"
echo "SCRIPT_NAME=$SCRIPT_NAME"
echo "PATH_INFO=$PATH_INFO"
echo "QUERY_STRING=$QUERY_STRING"
