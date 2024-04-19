#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/***********************************************************************
 *
 *  Procedure:
 *	SendInfo - send a command back to fvwm 
 *
 ***********************************************************************/
void SendInfo(int fd, const char *message, unsigned long window)
{
  if (NULL == message)
    return;

  int kg = 1; /* keep going */
  int len = strlen(message);
  int bufsz = sizeof(window) + sizeof(len) + len + sizeof(kg);
  char buf[bufsz];
  memcpy(buf, &window, sizeof(window));
  memcpy(buf + sizeof(window), &len, sizeof(len));
  memcpy(buf + sizeof(window) + sizeof(len), message, len);
  memcpy(buf + sizeof(window) + sizeof(len) + len, &kg, sizeof(kg));

  if (write(fd, buf, bufsz) != bufsz)
      fprintf(stderr, "%s: write error\n", __func__);
}


void SendFvwmPipe(int fd, const char *message, unsigned long window)
{
  while(1)
  {
    const char* temp = strchr(message, ',');
    if (NULL == temp) {
        SendInfo(fd, message, window);
        break;
    }

    int len = temp - message;
    char *temp_msg = malloc(len + 1);
    memcpy(temp_msg, message, len);
    temp_msg[len] = 0;
    SendInfo(fd, temp_msg, window);
    free(temp_msg);

    message = temp + 1;
  }
}
