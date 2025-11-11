/*
 * ViteS-ClienteFTP.c
 * Cliente FTP concurrente con comandos RFC959.
 * Implementa: USER, PASS, STOR, RETR, PORT, PASV
 * Extra: MKD, PWD, DELE, REST
 * Mantiene conexión de control mientras procesos hijos transfieren datos.
 * Requiere: connectTCP.c y errexit.c
 * Compilar: gcc -o ViteS-clienteFTP ViteS-clienteFTP.c connectTCP.c errexit.c
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LINELEN 4096

extern int errno;
int errexit(const char *format, ...);
int connectTCP(const char *host, const char *service);

/* ====================== FUNCIONES AUXILIARES ====================== */

void sendCmd(int s, char *cmd, char *res) {
  int n;
  n = strlen(cmd);
  if (n + 3 >= LINELEN) errexit("Comando demasiado largo");
  cmd[n] = '\r';
  cmd[n+1] = '\n';
  cmd[n+2] = '\0';
  if (write(s, cmd, n+2) < 0) errexit("write control socket: %s", strerror(errno));
  n = read(s, res, LINELEN-1);
  if (n < 0) errexit("read control socket: %s", strerror(errno));
  res[n] = '\0';
  printf("<-- %s", res);
}

int pasivo (int s){
  int sdata;
  int nport;
  char cmd[128], res[LINELEN], *p;
  char host[64], port[8];
  int h1,h2,h3,h4,p1,p2;

  strcpy(cmd, "PASV");
  sendCmd(s, cmd, res);
  p = strchr(res, '(');
  if (!p) return -1;
  if (sscanf(p+1, "%d,%d,%d,%d,%d,%d", &h1,&h2,&h3,&h4,&p1,&p2) != 6) return -1;
  snprintf(host, sizeof(host), "%d.%d.%d.%d", h1,h2,h3,h4);
  nport = p1*256 + p2;
  snprintf(port, sizeof(port), "%d", nport);
  sdata = connectTCP(host, port);
  return sdata;
}

int create_listen_socket(int *port_out) {
  int lsock;
  struct sockaddr_in addr;
  socklen_t alen = sizeof(addr);

  lsock = socket(AF_INET, SOCK_STREAM, 0);
  if (lsock < 0) return -1;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = 0;
  if (bind(lsock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(lsock); return -1; }
  if (listen(lsock, 1) < 0) { close(lsock); return -1; }
  if (getsockname(lsock, (struct sockaddr*)&addr, &alen) < 0) { close(lsock); return -1; }
  *port_out = ntohs(addr.sin_port);
  return lsock;
}

int send_PORT(int control_sock, int lsock, char *res) {
  struct sockaddr_in local;
  socklen_t len = sizeof(local);
  char hostbuf[64];
  int p, p1, p2;
  char cmd[128];

  if (getsockname(control_sock, (struct sockaddr*)&local, &len) < 0) return -1;
  snprintf(hostbuf, sizeof(hostbuf), "%s", inet_ntoa(local.sin_addr));
  struct sockaddr_in la; socklen_t alen = sizeof(la);
  if (getsockname(lsock, (struct sockaddr*)&la, &alen) < 0) return -1;
  p = ntohs(la.sin_port);
  p1 = p / 256; p2 = p % 256;
  int h1,h2,h3,h4;
  sscanf(hostbuf, "%d.%d.%d.%d", &h1,&h2,&h3,&h4);
  snprintf(cmd, sizeof(cmd), "PORT %d,%d,%d,%d,%d,%d", h1,h2,h3,h4,p1,p2);
  sendCmd(control_sock, cmd, res);
  return 0;
}

/* manejo de archivos */
void child_do_retr(int sdata, const char *filename) {
  FILE *fp = fopen(filename, "wb");
  if (!fp) { perror("fopen"); close(sdata); exit(1); }
  char buf[LINELEN]; ssize_t n;
  while ((n = recv(sdata, buf, sizeof(buf), 0)) > 0) fwrite(buf, 1, n, fp);
  fclose(fp);
  close(sdata);
  exit(0);
}

void child_do_stor(int sdata, const char *filename) {
  FILE *fp = fopen(filename, "rb");
  if (!fp) { perror("fopen"); close(sdata); exit(1); }
  char buf[LINELEN]; ssize_t n;
  while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
    if (write(sdata, buf, n) != n) break;
  }
  fclose(fp);
  close(sdata);
  exit(0);
}

void reap_children() {
  int status;
  while (waitpid(-1, &status, WNOHANG) > 0);
}

void print_centered_commands() {
  printf("\n===================== COMANDOS FTP SOPORTADOS =====================\n");
  printf("                   PWD              : mostrar directorio actual\n");
    printf("                   CD <archivo>                : Cambiar directorio\n");
  printf("                   MKD <dir>        : crear directorio\n");
  printf("                   DELE <archivo>   : eliminar archivo\n");
  printf("                   DIR / LS         : listar archivos\n");
  printf("                   GET <archivo>    : descargar (PASV)\n");
  printf("                   PUT <archivo>    : subir (PASV)\n");
  printf("                   PPUT <archivo>   : subir (PORT - activo)\n");
  printf("                   REST <byte>      : reanudar transferencia\n");
  printf("                   QUIT / BYE       : salir\n");
  printf("===================================================================\n\n");
}

/* ============================ MAIN ============================ */

int main(int argc, char *argv[]) {
  char *host = "localhost";
  char *service = "21";
  char cmd[LINELEN], res[LINELEN];
  char line[LINELEN];
  int s, n;
  pid_t pid;

  if (argc >= 2) host = argv[1];
  if (argc >= 3) service = argv[2];

  s = connectTCP(host, service);
  if (s < 0) errexit("No se pudo conectar al servidor FTP");

  n = read(s, res, sizeof(res)-1);
  if (n <= 0) errexit("No hay respuesta del servidor");
  res[n] = '\0';
  printf("<-- %s", res);

  /* Login */
  char usuario[64], clave[64];
  printf("\n=== AUTENTICACIÓN FTP ===\n");
  printf("Usuario: ");
  fgets(usuario, sizeof(usuario), stdin);
  usuario[strcspn(usuario, "\r\n")] = '\0';
  snprintf(cmd, sizeof(cmd), "USER %s", usuario);
  sendCmd(s, cmd, res);

  printf("Contraseña: ");
  fgets(clave, sizeof(clave), stdin);
  clave[strcspn(clave, "\r\n")] = '\0';
  snprintf(cmd, sizeof(cmd), "PASS %s", clave);
  sendCmd(s, cmd, res);

  print_centered_commands();

  printf("Cliente FTP concurrente listo.\nEscriba 'help' para ver comandos.\n");

  while (1) {
    reap_children();
    printf("ftp> ");
    if (!fgets(line, sizeof(line), stdin)) break;
    line[strcspn(line, "\r\n")] = '\0';
    if (strlen(line) == 0) continue;

    char *ucmd = strtok(line, " \t");
    if (!ucmd) continue;

    /* === comandos === */
    if (strcmp(ucmd, "quit") == 0 || strcmp(ucmd, "bye") == 0) {
      strcpy(cmd, "QUIT"); sendCmd(s, cmd, res); close(s); break;

    } else if (strcmp(ucmd, "help") == 0) {
      print_centered_commands();

    } else if (strcmp(ucmd, "pwd") == 0 || strcmp(ucmd, "mkd") == 0 || strcmp(ucmd, "dele") == 0) {
      char *arg = strtok(NULL, " \t");
      if (arg) snprintf(cmd, sizeof(cmd), "%s %s", ucmd, arg);
      else snprintf(cmd, sizeof(cmd), "%s", ucmd);
      sendCmd(s, cmd, res);

    } else if (strcmp(ucmd, "rest") == 0) {
      char *offset = strtok(NULL, " \t");
      if (!offset) { printf("Uso: REST <byte>\n"); continue; }
      snprintf(cmd, sizeof(cmd), "REST %s", offset);
      sendCmd(s, cmd, res);
} else if (strcmp(ucmd, "cd") == 0) {
      char *dir = strtok(NULL, " \t");
      if (!dir) {
        printf("Uso: cd <directorio>\n");
        continue;
      }

      snprintf(cmd, sizeof(cmd), "CWD %s", dir);
      sendCmd(s, cmd, res);

    } else if (strcmp(ucmd, "dir") == 0 || strcmp(ucmd, "ls") == 0) {
      int sdata = pasivo(s);
      if (sdata < 0) { printf("PASV fallo\n"); continue; }
      snprintf(cmd, sizeof(cmd), "LIST"); sendCmd(s, cmd, res);
      pid = fork();
      if (pid == 0) {
        char buf[LINELEN]; ssize_t r;
        while ((r = recv(sdata, buf, sizeof(buf), 0)) > 0) fwrite(buf,1,r,stdout);
        close(sdata); exit(0);
      } else {
        close(sdata);
        n = read(s, res, sizeof(res)-1);
        if (n > 0) { res[n]='\0'; printf("<-- %s", res); }
      }

    } else if (strcmp(ucmd, "get") == 0 || strcmp(ucmd, "retr") == 0) {
      char *filename = strtok(NULL, " \t"); if (!filename) { printf("Uso: get <archivo>\n"); continue; }
      int sdata = pasivo(s);
      if (sdata < 0) { printf("PASV fallo\n"); continue; }
      snprintf(cmd, sizeof(cmd), "RETR %s", filename);
      sendCmd(s, cmd, res);
      if (res[0] != '1') { close(sdata); continue; }
      pid = fork();
      if (pid == 0) child_do_retr(sdata, filename);
      close(sdata);
      n = read(s, res, sizeof(res)-1);
      if (n > 0) { res[n]='\0'; printf("<-- %s", res); }

    } else if (strcmp(ucmd, "put") == 0 || strcmp(ucmd, "stor") == 0) {
      char *filename = strtok(NULL, " \t"); if (!filename) { printf("Uso: put <archivo>\n"); continue; }
      int sdata = pasivo(s);
      if (sdata < 0) { printf("PASV fallo\n"); continue; }
      snprintf(cmd, sizeof(cmd), "STOR %s", filename);
      sendCmd(s, cmd, res);
      if (res[0] != '1') { close(sdata); continue; }
      pid = fork();
      if (pid == 0) child_do_stor(sdata, filename);
      close(sdata);
      n = read(s, res, sizeof(res)-1);
      if (n > 0) { res[n]='\0'; printf("<-- %s", res); }

    } else if (strcmp(ucmd, "pput") == 0 || strcmp(ucmd, "port") == 0) {
      char *filename = strtok(NULL, " \t"); if (!filename) { printf("Uso: pput <archivo>\n"); continue; }
      int lsock, chosen_port;
      lsock = create_listen_socket(&chosen_port);
      if (lsock < 0) { perror("create_listen_socket"); continue; }
      if (send_PORT(s, lsock, res) < 0) { close(lsock); printf("No pudo enviar PORT\n"); continue; }
      snprintf(cmd, sizeof(cmd), "STOR %s", filename); sendCmd(s, cmd, res);
      if (res[0] != '1') { close(lsock); continue; }
      pid = fork();
      if (pid == 0) {
        int ns; struct sockaddr_in peer; socklen_t al = sizeof(peer);
        ns = accept(lsock, (struct sockaddr*)&peer, &al);
        if (ns < 0) { perror("accept"); close(lsock); exit(1); }
        close(lsock);
        child_do_stor(ns, filename);
      } else {
        close(lsock);
        n = read(s, res, sizeof(res)-1);
        if (n > 0) { res[n]='\0'; printf("<-- %s", res); }
      }

    } else {
      snprintf(cmd, sizeof(cmd), "%s", line);
      sendCmd(s, cmd, res);
    }
  }

  while (wait(NULL) > 0);
  close(s);
  return 0;
}

