/* Kleiner Client fuer MQTT (mit Mosquitto erstellt), der Nachrichten per Sprache aus-
 * geben kann.
 * Relevante Links: https://mosquitto.org/api/files/mosquitto-h.html
 *                  https://github.com/eclipse/mosquitto
 */
#ifndef MQTT_SIMPLE_H
#define MQTT_SIMPLE_H

#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>

#include <unistd.h>
#include <mosquitto.h>
#include <pthread.h>
#include <sys/wait.h>
#include <fcntl.h> /* Fuer File-Descriptor-Umbiegung (ALSA) */

#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT 1883
#define DEFAULT_TOPIC "#"
#define DEFAULT_QOS 1

static char * host;
static char * topic;
static int port;
const char * script;
static int verbose;

/* Filedescriptoren, werden fuer das Umlenken von stderr genommen, sind fuer atexit() sta-
 * tisch und nicht in Funktion 
 */
static int devnull_fd,
           return_fd;

/* Zwei Mutexe fuer Stdout und den Voice-Output. Im Moment nicht zwingend noetig, da das 
 * Programm in einem einzelnen Thread laueft (Der Voice-Output wird durch wait(&status) 
 * in VoiceOutput() bedingt garantiert nicht mehrmals gleichzeitig aufgerufen). Wird aber
 * essentiell, falls jemals mehr als ein Thread aufgemacht werden sollte. 
 */
pthread_mutex_t print_mutex;
pthread_mutex_t voice_mutex;

/* rc:      Return-Code, gibt den Status der Verbindung zum Broker wieder (wird fuer 
 *          Signal-Handle global deklariert)
 * mosq:    das Mosquitto-Struct fuer die Session
 */
int rc = 0;
static struct mosquitto * mosq;

/*** Settings, die spaeter in mosq eingetragen werden ***/
static char * host;
static char * topic;
static int port;
const char * script;
static int verbose;

/* Verwendete Hilfsfunktionen */
extern void print_manual();
extern void exit_cleanup(); /* Callback-Funktion, die beim Aufruf von exit() verwendet wird */
extern int voiceOutput (char * message);

/* Callbacks, die nach dem Initialisieren von mosq hinterlegt werden */
extern void message_callback(struct mosquitto * mosq, void * obj, const struct mosquitto_message * message);
extern void connect_callback(struct mosquitto * mosq, void * obj, int result);
extern void disconnect_callback(struct mosquitto * mosq,  void * obj, int rc);
extern void log_callback(struct mosquitto * mosq, void * obj, int level, const char * str);

#endif
