# Computacion_Distribuida

El cliente usa **dos tipos de conexión**:
- **Puerto 21 (control):** para enviar comandos y recibir respuestas del servidor.
- **Puertos dinámicos (datos, PASV):** para transferir archivos (`get`, `put`) de forma concurrente.

---
Implementación de comandos básicos FTP:
- `USER` — Enviar nombre de usuario.  
- `PASS` — Enviar contraseña.  
- `STOR` — Subir archivos al servidor.  
- `RETR` — Descargar archivos del servidor.  
- `PASV` — Activar modo pasivo para transferencias.  
- `PORT` — Conexión activa manual.  
- `CWD` — Cambiar de directorio remoto (`cd`).  
- `QUIT` — Finalizar sesión.

- Funcionalidades extra:
- **Concurrencia:** cada transferencia (`get` o `put`) se ejecuta en un proceso hijo mediante `fork()`.  
- **Sesión persistente:** tras autenticarse una vez, no vuelve a pedir usuario ni contraseña.  
- **Mensajes informativos:** muestra PID de cada proceso hijo creado.  
- **Compatibilidad con servidores estándar:** probado con `vsftpd` y `proftpd`.

---
## ⚙️ Compilación

Ejecuta en la terminal:

make
./ViteS-ClienteFTP

Nota: En modo PASV, el servidor abre un puerto de datos aleatorio (>1024).

📘 Referencias

- RFC 959 — File Transfer Protocol (FTP):
https://datatracker.ietf.org/doc/html/rfc959
- vsftpd Documentation:
https://security.appspot.com/vsftpd.html
