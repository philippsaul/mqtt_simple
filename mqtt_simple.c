#include "mqtt_simple.h"

/* Manual */
void print_manual() {
    pthread_mutex_lock(&print_mutex);
    fprintf(stdout, "Verfuegbare Launch-Optionen:\n"
                    "-h: Host\tDefault: localhost\n"
                    "-m: Manual\tPrint this Manual\n"
                    "-p: Port\tDefault: 1883\n"
                    "-q: QoS\t\tDefault: 2\n"
                    "-s: Script\tDefault: espeak -v mb-de2 [MESSAGE]. Erwartet Pfad zu Bash-Skript\n"
                    "-t: Topic\tDefault: #\n"
                    "-v: Verbose\tPrint received messages to Stdout\n");
    pthread_mutex_unlock(&print_mutex);
}

/* atexit()-Callback */
void exit_cleanup () {
    close(devnull_fd);
    close(return_fd);
    pthread_mutex_destroy(&voice_mutex);
    pthread_mutex_destroy(&print_mutex);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
}

/* Behandelt Interrupt-Signal, aktuell nur SIGINT und SIGTERM, siehe main() */
void handle_signal(int s) {
    fprintf(stdout, "Shutting Down..\n");
	mosquitto_disconnect(mosq);
	exit(rc);
}

/* Sprachausgabe von message, entweder ueber Default (espeak) oder beim Start angegebenes.
 * Ein minimalistisches Script fuer bash mit pico2wave koennte beispielsweise so aussehen:
 *
 * #!/bin/bash
 * pico2wave -w /tmp/message.wav -l "de-DE" "$@"
 * aplay /tmp/message.wav
 * rm /tmp/message.wav
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
  	    fprintf(stdout, "Got message '%.*s' for topic '%s'\n", message->payloadlen, (char *) message->payload, message->topic);
    pthread_mutex_unlock(&print_mutex);

  	mosquitto_topic_matches_sub(topic , message->topic, &match);
  	if (match) {
        voiceOutput ((char *) message->payload);
  	}
}

void connect_callback(struct mosquitto * mosq, void * obj, int result) {
    pthread_mutex_lock(&print_mutex);
  //fprintf(stdout, "Start with -m flag to see the manual\n");
	fprintf(stdout, "Connected\n");
    pthread_mutex_unlock(&print_mutex);
}

void disconnect_callback(struct mosquitto * mosq,  void * obj, int rc) {
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
}

/* Funktion wird gerade nicht verwendet, kann aber schnell erweitert werden */
void log_callback(struct mosquitto * mosq, void * obj, int level, const char * str) {
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

    /* Argument-Parsing der Launch-Argumente und Init der Settings */
    int host_flag = 0,
        man_flag = 0,
        port_flag = 0,
        qos_flag = 0,
        script_flag = 0,
        topic_flag = 0;
    int c;
    opterr = 0; /* Verhindert in getopt() einen Print auf stderr, falls option character 
                   unbekannt */

    while ((c = getopt(argc, argv, "h:mp:q:s:t:v")) != -1) {
        switch (c) {
            case 'h':
                host_flag = 1;
                host = optarg;
                break;
            case 'm':
                man_flag = 1;
                break;
            case 'p':
                port_flag = 1;
                port = (int) strtol(optarg, NULL, 0);
                break;
            case 'q':
                qos_flag = 1;
                qos = (int) strtol(optarg, NULL, 0);
            case 's':
                script_flag = 1;
                script = optarg;
                break;
            case 't':
                topic_flag = 1;
                topic = optarg;
                break;
            case 'v':
                verbose = 1;
                break;
            case '?':
                if (optopt == 'h' || optopt == 'p' || optopt == 's' || optopt == 't') {
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
    /* Ende Argument-Parsing und Init */

    host = (host_flag == 0) ? DEFAULT_HOST : host;
    port = (port_flag == 0) ? DEFAULT_PORT : port;
    qos = (qos_flag == 0) ? DEFAULT_QOS : qos;
    topic = (topic_flag) == 0 ? DEFAULT_TOPIC : topic;
    script = (script_flag) == 0 ? NULL : script;

    /* Manual und Quit */
    if (man_flag) {
        print_manual();
        exit(0);
    }

    /* Mutex-Init */
    pm_check = pthread_mutex_init(&print_mutex, NULL);
    vm_check = pthread_mutex_init(&voice_mutex, NULL);
    if ((pm_check || vm_check) != 0) {
        perror("Mutex Init");
        _exit(1);
    }

    /* Lenkt stderr in /dev/null um, da ALSA zu viel schwaetzt und Stdout zumuellt */
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
    mosq = mosquitto_new("RasPI-Session", true, 0); /* Return NULL on Failure */

    if(mosq) {
        mosquitto_connect_callback_set(mosq, connect_callback);
        mosquitto_message_callback_set(mosq, message_callback);
        mosquitto_disconnect_callback_set(mosq, disconnect_callback);
        mosquitto_log_callback_set(mosq, log_callback);

        rc = mosquitto_connect(mosq, host, port, 60);
        if (rc == MOSQ_ERR_INVAL) {
            pthread_mutex_lock(&print_mutex);
            fprintf(stdout, "Fehlerhafte Konfigurationsdaten!\n");
            pthread_mutex_unlock(&print_mutex);
            exit(rc);
        }
        else if (rc == MOSQ_ERR_ERRNO) {
            pthread_mutex_lock(&print_mutex);
            fprintf (stdout, "Error: %s\n", strerror(errno));
            pthread_mutex_unlock(&print_mutex);
            exit(rc);
            }

        mosquitto_subscribe(mosq, NULL, topic, qos);
        rc = mosquitto_loop_forever(mosq, -1, 1);
    }
    /* Dieser Teil wird nur dann jemals ausgefuehrt falls mosq == NULL. Ansonsten uebernimmt
     * atexit() das Aufraeumen, da das Programm nur ueber Interrupt terminiert wird
     */
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return rc;
}
