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
#include <fcntl.h>

#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT 1883
#define DEFAULT_TOPIC "#"
#define DEFAULT_QOS 2

//rc ist der Return-Code (wird fuer Signal-Handle global deklariert), mosq das Mosquitto-Struct fuer die Session
int rc = 0;
static struct mosquitto * mosq;

//Settings
static char * host;
static char * topic;
static int port;
const char * script;
static int verbose;

//Filedescriptoren, werden fuer das Umlenken von stderr genommen, sind fuer atexit() statisch und nicht in Funktion
static int  devnull_fd,
            return_fd;

pthread_mutex_t print_mutex;
pthread_mutex_t voice_mutex;

//Manual
void print_manual() {
    pthread_mutex_lock(&print_mutex);
    fprintf(stdout, "-h: Host\tDefault: localhost\n"
                    "-m: Manual\n"
                    "-p: Port\tDefault: 1883\n"
                    "-s: Script\tDefault: /bin/espeak espeak -v mb-de2 [MESSAGE]. Erwartet Pfad zu Shell-Skript\n"
                    "-t: Topic\tDefault: #\n"
                    "-v: Verbose\n");
    pthread_mutex_unlock(&print_mutex);
}

//atexit()
void exit_cleanup () {
    close(devnull_fd);
    close(return_fd);
//    pthread_mutex_destroy(&voice_mutex);
//    pthread_mutex_destroy(&print_mutex);
}

//Handled Interrupt-Signal (aktuell nur SIGINT und SIGTERM)
void handle_signal(int s) {
    printf("Shutting Down..\n");
	mosquitto_disconnect(mosq);
	exit(rc);
}

//Selbsterklaerend: Sprachausgabe von message
int voiceOutput (char * message) {
    int status;
    pthread_mutex_lock(&voice_mutex);
    pid_t child = fork();
    if (child >= 0) {
        if (child == 0) {
            if (script != NULL)
                return execl("/bin/bash", "/bin/bash", script, message, NULL);
            else
                return execl("/bin/espeak", "espeak", "-v", "mb-de2", message, NULL);
        }
        else {
            wait (&status);
        }
    }
    else {
        perror("Fork Failed");
        pthread_mutex_unlock(&voice_mutex);
        return 1;
    }
    pthread_mutex_unlock(&voice_mutex);
    return 0;
}

//Die Callback-Funktionen
void message_callback(struct mosquitto * mosq, void * obj, const struct mosquitto_message * message) {
	bool match = 0;
    pthread_mutex_lock(&print_mutex);
    if (verbose)
  	    printf("got message '%.*s' for topic '%s'\n", message->payloadlen, (char *) message->payload, message->topic);
    pthread_mutex_unlock(&print_mutex);

  	mosquitto_topic_matches_sub(topic , message->topic, &match);
  	if (match) {
        voiceOutput ((char *) message->payload);
  	}
}

void connect_callback(struct mosquitto * mosq, void * obj, int result) {
    pthread_mutex_lock(&print_mutex);
	printf("Connected\n");
    pthread_mutex_unlock(&print_mutex);
}

void disconnect_callback(struct mosquitto * mosq,  void * obj, int rc) {
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
}


int main(int argc, char ** argv) {
    //Fuer Mutex-Check
    int pm_check,
        vm_check;

    //Initialisierung der Interrupt-Handle-Funktion/ exit()-Funktion
    atexit(exit_cleanup);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    //Argument-Parsing der Launch-Argumente
    int host_flag = 0,
        man_flag = 0,
        port_flag = 0,
        script_flag = 0,
        topic_flag = 0,
        verbose_flag = 0;
    char * hostval = NULL;
    int portval;
    char * scriptval = NULL;
    char * topicval = NULL;
    int c;
    opterr = 0;

    while ((c = getopt(argc, argv, "h:mp:s:t:v")) != -1) {
        switch (c) {
            case 'h':
                host_flag = 1;
                hostval = optarg;
                break;
            case 'p':
                port_flag = 1;
                portval = (int) strtol(optarg, NULL, 0); //Int-Cast, um die GCC-Warning zu beseitigen
                if (portval == 0) {
                    print_manual();
                    exit(1);
                }
                break;
            case 'm':
                man_flag = 1;
                break;
            case 's':
                script_flag = 1;
                scriptval = optarg;
                break;
            case 't':
                topic_flag = 1;
                topicval = optarg;
                break;
            case 'v':
                verbose_flag = 1;
                break;
            case '?':
                if (optopt == 'h' || optopt == 'p' || optopt == 't') {
                    pthread_mutex_lock(&print_mutex);
                    fprintf(stderr, "-%c hat falsches oder kein Argument\n", optopt);
                    pthread_mutex_unlock(&print_mutex);
                }
                else if (isprint(optopt)) {
                    pthread_mutex_lock(&print_mutex);
                    fprintf(stderr, "-%c ist unbekannte Option.\n", optopt);
                    pthread_mutex_unlock(&print_mutex);
                }
                else {
                    pthread_mutex_lock(&print_mutex);
                    fprintf(stderr, "Unbekannter Character -%c\n", optopt);
                    pthread_mutex_unlock(&print_mutex);
                }
                print_manual();
                exit(1);
            default:
                print_manual();
                exit(1);
        }
    }
    //Ende Argument-Parsing

    //Manual und Quit
    if (man_flag) {
        print_manual();
        exit(0);
    }

    //Initialisierung der Parameter
    host = (host_flag == 1 ? hostval : DEFAULT_HOST);
    port = (port_flag == 1 ? portval : DEFAULT_PORT);
    script = (script_flag == 1 ? scriptval : NULL);
    topic = (topic_flag == 1 ? topicval : DEFAULT_TOPIC);
    verbose = (verbose_flag == 1 ? 1 : 0);

	//Lenkt stderr in /dev/null um, da ALSA zu viel schwaetzt und stdout zumuellt
    devnull_fd = open("/dev/null", O_WRONLY);
    if (devnull_fd == -1) {
        perror("open() /dev/null");
        _exit(1);
	}

	return_fd = dup2(devnull_fd, 2);
    if (return_fd < 0) {
        perror("dup2()");
        _exit(1);
    }

    //Mutex-Init
    pm_check = pthread_mutex_init(&print_mutex, NULL);
    vm_check = pthread_mutex_init(&voice_mutex, NULL);
    if ((pm_check || vm_check) != 0) {
        perror("Mutex Init");
        _exit(1);
    }

    //Hier geht Mosquitto los
	mosquitto_lib_init();
	mosq = mosquitto_new("RasPI-Session", true, 0);

	if(mosq) {
		mosquitto_connect_callback_set(mosq, connect_callback);
		mosquitto_message_callback_set(mosq, message_callback);
		mosquitto_disconnect_callback_set(mosq, disconnect_callback);

	    rc = mosquitto_connect(mosq, host, port, 60);
	    if (rc != 0) {
	        pthread_mutex_lock(&print_mutex);
	        fprintf(stdout, "Fehlerhafte Konfigurationsdaten oder keine Verbindung!\n"
                            "Shutting Down..\n");
	        pthread_mutex_unlock(&print_mutex);
	        exit(rc);
	    }
		mosquitto_subscribe(mosq, NULL, topic, 0);

        rc = mosquitto_loop_forever(mosq, -1, 1);
	}
	//Dieser Teil wird nur dann jemals ausgefuehrt, falls mosq == 0. Ansonsten atexit()
    mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
	exit(rc);
}