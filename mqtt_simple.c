#include "mqtt_simple.h"

/* Manual */
void print_manual() {
    pthread_mutex_lock(&print_mutex);
    fprintf(stdout, "Verfuegbare Launch-Optionen:\n"
                    "-h: Host\tDefault: localhost\n"
                    "-m: Manual\n"
                    "-p: Port\tDefault: 1883\n"
                    "-s: Script\tDefault: espeak -v mb-de2 [MESSAGE]. Erwartet Pfad zu Shell-Skript\n"
                    "-t: Topic\tDefault: #\n"
                    "-v: Verbose\n");
    pthread_mutex_unlock(&print_mutex);
}

/* atexit()-Callback */
void exit_cleanup () {
    close(devnull_fd);
    close(return_fd);
    pthread_mutex_destroy(&voice_mutex);
    pthread_mutex_destroy(&print_mutex);
}

/* Handled Interrupt-Signal, aktuell nur SIGINT und SIGTERM, siehe main() */
void handle_signal(int s) {
    printf("Shutting Down..\n");
	mosquitto_disconnect(mosq);
	exit(rc);
}

/* Sprachausgabe von message, entweder ueber Default (espeak) oder beim Start angegebenes.
 * Ein minimalistisches Script fuer bash koennte beispielsweise so aussehen:
 *
 * #!/bin/bash
 * pico2wave -w /tmp/message.wav -l "de-DE" "$@"
 * aplay /tmp/message.wav
 * rm /tmp/message.wav
 * return 0
 */
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

/* Die Callback-Funktionen */
void message_callback(struct mosquitto * mosq, void * obj, const struct mosquitto_message * message) {
	bool match = 0;
    pthread_mutex_lock(&print_mutex);
    if (verbose)
  	    printf("Got message '%.*s' for topic '%s'\n", message->payloadlen, (char *) message->payload, message->topic);
    pthread_mutex_unlock(&print_mutex);

  	mosquitto_topic_matches_sub(topic , message->topic, &match);
  	if (match) {
        voiceOutput ((char *) message->payload);
  	}
}

void connect_callback(struct mosquitto * mosq, void * obj, int result) {
    pthread_mutex_lock(&print_mutex);
    printf ("Start with -m flag to see the manual\n");
	printf("Connected\n");
    pthread_mutex_unlock(&print_mutex);
}

void disconnect_callback(struct mosquitto * mosq,  void * obj, int rc) {
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
}

/* Funktion wird gerade nicht verwendet, kann aber schnell erweitert werden */
void log_callback(struct mosquitto * mosq, void * obj, int level, const char * str) {
//TODO SET LOGFILES
    ;
}

int main(int argc, char ** argv) {
    /* Fuer Mutex-Init-Ueberpruefung */
    int pm_check,
        vm_check;

    /* Initialisierung der Interrupt-Handle-Funktion/ exit()-Funktion */
    atexit(exit_cleanup);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Argument-Parsing der Launch-Argumente */
    int host_flag = 0,
        man_flag = 0,
        port_flag = 0,
        script_flag = 0,
        topic_flag = 0,
        verbose_flag = 0;
    int portval;
    char * hostval = NULL;
    char * scriptval = NULL;
    char * topicval = NULL;
    int c;
    opterr = 0; /* Verhindert in getopt() einen Print auf stderr, falls option character 
                   unbekannt */

    while ((c = getopt(argc, argv, "h:mp:s:t:v")) != -1) {
        switch (c) {
            case 'h':
                host_flag = 1;
                hostval = optarg;
                break;
            case 'p':
                port_flag = 1;
                portval = (int) strtol(optarg, NULL, 0); /*Int-Cast, um die GCC-Warning zu
                                                           beseitigen */
                if (portval == 0 || portval > 65535) {
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
    /* Ende Argument-Parsing */

    /* Manual und Quit */
    if (man_flag) {
        print_manual();
        exit(0);
    }

    /* Initialisierung der Settings */
    host = (host_flag == 1 ? hostval : DEFAULT_HOST);
    port = (port_flag == 1 ? portval : DEFAULT_PORT);
    script = (script_flag == 1 ? scriptval : NULL);
    topic = (topic_flag == 1 ? topicval : DEFAULT_TOPIC);
    verbose = (verbose_flag == 1 ? 1 : 0);

    /* Mutex-Init */
    pm_check = pthread_mutex_init(&print_mutex, NULL);
    vm_check = pthread_mutex_init(&voice_mutex, NULL);
    if ((pm_check || vm_check) != 0) {
        perror("Mutex Init");
        _exit(1);
    }

    /* Lenkt stderr in /dev/null um, da ALSA zu viel schwaetzt und stdout zumuellt */
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

    /* Hier geht Mosquitto los */
    mosquitto_lib_init();
    mosq = mosquitto_new("RasPI-Session", true, 0);

    if(mosq) {
        mosquitto_connect_callback_set(mosq, connect_callback);
        mosquitto_message_callback_set(mosq, message_callback);
        mosquitto_disconnect_callback_set(mosq, disconnect_callback);
        mosquitto_log_callback_set(mosq, log_callback);

        rc = mosquitto_connect(mosq, host, port, 60);
        if (rc != 0) {
            pthread_mutex_lock(&print_mutex);
            fprintf(stdout, "Fehlerhafte Konfigurationsdaten oder keine Verbindung!\n");
            pthread_mutex_unlock(&print_mutex);
            exit(rc);
        }

        mosquitto_subscribe(mosq, NULL, topic, DEFAULT_QOS);
        rc = mosquitto_loop_forever(mosq, -1, 1);
    }
    /* Dieser Teil wird nur dann jemals ausgefuehrt, falls mosq == 0. Ansonsten atexit()
     * (Da das Programm sowieso terminiert wird, kann auch das Betriebssystem das Auf-
     * raeumen uebernehmen)
     */
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    exit(rc);
}
