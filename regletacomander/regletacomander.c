/******************************************************************************
 * miniterm.c
 *
 * Adapted from the example program distributed with the Linux Programmer's
 * Guide (LPG). This has been robustified and tweaked to work as a debugging
 * terminal for Xen-based machines.
 *
 * Modifications are released under GPL and copyright (c) 2003, K A Fraser
 * The original copyright message and license is fully intact below.
 */

/*
*Autor Angel Lanza a.lanza10@gmail.com
*Ejecución para control regleta 4 relay
*Comandos soportados (14 chars):
*Status manda SXXXXXXXXXXXXXX
*Rele ON|OFF NUM manda R[E,A][1-4]XXXXXXXXXXX
*Sync horas manda T10CHAREPOCHXXX
*Programar Num INICIO FIN  manda P[1-4]HHMMSSHHMMSS (inicio-fin)
*/


#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>

//control & protocol
#define MSG_LEN  14
#define RELAY_MSG_LEN 3
#define RELAY_MSG_LEN_TIMED  5
#define TIME_HEADER  'T'
#define SHOW_SATUS_HEADER  'S'
#define PROG_HEADER  'P'
#define RELAY_HEADER  'R'
#define RELAY_ON  'E'
#define RELAY_OFF  'A'
#define DURATION 'D'
#define RELAY_COUNT 4
#define TIME_REQUEST  '\a' //7 ASCII code BELL in C
#define COMPLETE_CHAR 'X'
#define TZ_ADJUST +2
#define RELAY_1 '1'
#define RELAY_2 '2'
#define RELAY_3 '3'
#define RELAY_4 '4'
#define HELP '?'

#define DEFAULT_BAUDRATE   115200
#define DEFAULT_SERDEVICE  "/dev/ttyAMA0"
#define ENDMINITERM        0x1d

volatile int stop = 0;

void child_handler(int s)
{
    stop = 1;
}

int cook_baud(int baud)
{
    int cooked_baud = 0;
    switch ( baud )
    {
    case     50: cooked_baud =     B50; break;
    case     75: cooked_baud =     B75; break;
    case    110: cooked_baud =    B110; break;
    case    134: cooked_baud =    B134; break;
    case    150: cooked_baud =    B150; break;
    case    200: cooked_baud =    B200; break;
    case    300: cooked_baud =    B300; break;
    case    600: cooked_baud =    B600; break;
    case   1200: cooked_baud =   B1200; break;
    case   1800: cooked_baud =   B1800; break;
    case   2400: cooked_baud =   B2400; break;
    case   4800: cooked_baud =   B4800; break;
    case   9600: cooked_baud =   B9600; break;
    case  19200: cooked_baud =  B19200; break;
    case  38400: cooked_baud =  B38400; break;
    case  57600: cooked_baud =  B57600; break;
    case 115200: cooked_baud = B115200; break;
    }
    return cooked_baud;
}

int main(int argc, char **argv)
{
    int              fd, c, cooked_baud = cook_baud(DEFAULT_BAUDRATE),i=0;
    char            *sername = DEFAULT_SERDEVICE,token,*command;
    char extra,relay,mode;
    char time_msg[10];
    char prog_time[] = "000000000000";
    time_t sec;
    struct termios   oldsertio, newsertio, oldstdtio, newstdtio;
    struct sigaction sa;
    static char status_str[] = "S1111111111111";
    static char start_str[] =
        "************ CONSOLA REGLETA: AYUDA ? | CTRL-] SALIR ********\r\n";
    static char end_str[] =
        "\n************ SALIR DE CONSOLA *****************\n";
    static char help_str[] =
        "Tasks: begin with -t\r\n R[E,A][1-4](reles)\r\n T (sync time)\r\n P[1-4]HHMMSSHHMMSS (programar)\r\n";

    if ( argc == 1 ){
    	goto usage;
    }

    while ( --argc != 0 )
    {
        char *p = argv[argc];
        if ( *p++ != '-' )
            goto usage;
        if ( *p == 'b' )
        {
            p++;
            if ( (cooked_baud = cook_baud(atoi(p))) == 0 )
            {
                fprintf(stderr, "Bad baud rate '%d'\n", atoi(p));
                goto usage;
            }
        }
        else if ( *p == 'd' )
        {
            sername = ++p;
            if ( *sername == '\0' )
                goto usage;
        }
        else if ( *p == 't' )
        {
            command = ++p;
            if ( *command == '\0' )
                goto usage;
        }
        else
            goto usage;
    }

    /* Not a controlling tty: CTRL-C shouldn't kill us. */
    fd = open(sername, O_RDWR | O_NOCTTY);
    if ( fd < 0 )
    {
        perror(sername);
        exit(-1);
    }

    tcgetattr(fd, &oldsertio); /* save current modem settings */

    /*
     * 8 data, no parity, 1 stop bit. Ignore modem control lines. Enable
     * receive. Set appropriate baud rate. NO HARDWARE FLOW CONTROL!
     */
    newsertio.c_cflag = cooked_baud | CS8 | CLOCAL | CREAD;

    /* Raw input. Ignore errors and breaks. */
    newsertio.c_iflag = IGNBRK | IGNPAR;

    /* Raw output. */
    newsertio.c_oflag = OPOST;

    /* No echo and no signals. */
    newsertio.c_lflag = 0;

    /* blocking read until 1 char arrives */
    newsertio.c_cc[VMIN]=1;
    newsertio.c_cc[VTIME]=0;

    /* now clean the modem line and activate the settings for modem */
    tcflush(fd, TCIFLUSH);
    tcsetattr(fd,TCSANOW,&newsertio);

    /* next stop echo and buffering for stdin */
    tcgetattr(0,&oldstdtio);
    tcgetattr(0,&newstdtio); /* get working stdtio */
    newstdtio.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    newstdtio.c_oflag &= ~OPOST;
    newstdtio.c_cflag &= ~(CSIZE | PARENB);
    newstdtio.c_cflag |= CS8;
    newstdtio.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    newstdtio.c_cc[VMIN]=1;
    newstdtio.c_cc[VTIME]=0;
    tcsetattr(0,TCSANOW,&newstdtio);

    token = (char)RELAY_HEADER;
	extra = (char)COMPLETE_CHAR;
	mode=(char)"A";
	relay=(char)"1";
	write(fd,&token,1);
	write(fd,&mode,1);
	write(fd,&relay, 1);
	write(fd,&extra,1);
	write(fd,&extra,1);
	write(fd,&extra,1);
	write(fd,&extra,1);
	write(fd,&extra,1);
	write(fd,&extra,1);
	write(fd,&extra,1);
	write(fd,&extra,1);
	write(fd,&extra,1);
	write(fd,&extra,1);
	write(fd,&extra,1);

    tcsetattr(fd,TCSANOW,&oldsertio);
    close(fd);
    printf("Adios\n");
    close(0);
    close(1);
    return 0;

 usage:
    printf("regletacomander [-b<baudrate>] [-d<devicename>] -ttask\n");
    printf("Default baud rate: %d\n", DEFAULT_BAUDRATE);
    printf("Default device: %s\n", DEFAULT_SERDEVICE);
    printf("%s", help_str);
    printf("Example: regletacomander -b57600 -d/dev/ttyAMA0 -tRE1\n\r");
    return 1;
}

