/*
	main.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>

#include "./include/bot.h"
#include "./include/irc-datatypes.h"
#include "./include/helper-functions.h"
#include "./include/arr.h"
#include "./include/connect.h"
#include "./include/parsers.h"
#include "./include/privmsg-funcs.h"

int main(int argc, char** argv) {

	if (argc != 5) {
		printf("[!] Usage: %s [server] [nick] [password] [admin]\n", argv[0]);
		exit(4);
	}

	/*
		Streamlining IRC Data into a session.
	*/
	IRCSession *session = malloc(sizeof(IRCSession));
	init_session(session, argv[1], argv[2], argv[3], IRC_PORT, argv[4]);

	char* echoing = NULL;
	int restart = 1, iplookupset = 0, sleeping = 0;

	while(restart) {

		restart = 0;

		time_t t;
		srand((unsigned) time(&t));

		connect_to_irc(session);
		log_on(session);

		char *buf, *out;
		buf = malloc(sizeof(char) * BUFFER_SIZE+1);
		out = malloc(sizeof(char) * BUFFER_SIZE+1);

		/* now we let the first admin know we are ready */
		if (session->num_admins < 1) {
			printf("No admin? Exiting...\n");
			exit(5);
		} else {
			write_to_socket(session, out, "\rPRIVMSG %s :Ready for commands.\r\n", session->admins[0]);
		}

		IRCPacket *packet = malloc(sizeof(IRCPacket));
		Command *command = malloc(sizeof(Command));

		int n, admin_is_sender = 0, ignore_sender = 0;

		while( (n = read(session->sockfd, buf, BUFFER_SIZE)) && !restart) {

			buf[n] = 0x00;

			printf("\n%s\n", buf);

			if (!strncmp(buf, "PING", 4)) {

				buf[1] = 'O';
				write_to_socket(session, out, buf);
				printf("[*] Pong sent!\n");
				continue;

			}

			if (parse_irc_packet(buf, packet) <= 0) {
				continue;
			}

			admin_is_sender = (arr_find(session->admins, packet->sender, &session->num_admins) != NULL);
			ignore_sender = (arr_find(session->ignoring, packet->sender, &session->num_ignoring) != NULL);

			if (packet->content != NULL && !strcmp(packet->type, "PRIVMSG") && !ignore_sender) {

				/*
					Must run echo check first.
				*/
				if (echoing != NULL && !strcmp(packet->sender, echoing)) {
					write_to_socket(session, out, "\rPRIVMSG %s :%s\r\n", packet->channel, packet->content);
				}

				/*
					No need for the name prompt (e.g., "pinetree: ") in a PM. Otherwise...
					Only speak when spoken to, and don't execute commands said by yourself.

				*/
				if (strcmp(packet->channel, session->nick) && (strlen(packet->content) < strlen(session->nick)
					|| strncmp(packet->content, session->nick, strlen(session->nick))
					|| !strcmp(session->nick, packet->sender))) {
					continue;
				}

				/*
					If we are in a PM prepare to send things back, unlike when
					in a channel.
				*/
				if (!strcmp(packet->channel, session->nick)) {
					packet->channel = packet->sender;
				}

				/*
					Going to parse for any command.
					First found, actually.
				*/

				if (parse_for_command(packet, command) == 0) {
					continue;
				}

				if ( !strcmp(command->cmd, "wakeup")) {
					sleeping = 0;

					write_to_socket(session, out, "\rPRIVMSG %s :\001ACTION yawned!\001\r\n", packet->channel);

					sleep(0.7);

					write_to_socket(session, out, "\rPRIVMSG %s :What'd I miss?\r\n", packet->channel);
				}

				if (sleeping) continue;

				if ( !strcmp(command->cmd, "slap") && command->argc >= 1) {

					slap(session, packet, out, command);

				} else if ( !strcmp(command->cmd, "google") && command->argc >= 1) {

					query(session, packet, out, command, "http://google.com/#q=");

				} else if ( !strcmp(command->cmd, "search") && command->argc >= 1) {

					search(session, packet, out, command);

				} else if ( !strcmp(command->cmd, "urban") && command->argc >= 1) {

					query(session, packet, out, command, "https://www.urbandictionary.com/define.php?term=");

				} else if ( !strcmp(command->cmd, "topic") && command->argc >= 1) {

					write_to_socket(session, out, "\rPRIVMSG %s :https://0x00sec.org/t/%d\r\n", packet->channel, atoi(command->argv[0]));

				} else if ( !strcmp(command->cmd, "iplookup") && command->argc >= 1) {

					if (iplookupset) {
						char* pos = command->argv[0];
						pos = strtok(pos, " /;,&|~!?\?+=#@%%\\$>^*()[]{}_<\"\'\r\b`");
						printf("[*] host = %s.\n", pos);

						if (pos != NULL) ip_lookup(pos, out, session, packet);
					} else {
						write_to_socket(session, out, "\rPRIVMSG %s :%s: iplookup is off.\r\n", packet->channel, packet->sender);
					}

				} else if ( !strcmp(command->cmd, "help") ) {

					write_to_socket(session, out, "\rPRIVMSG %s :slap, google, search, urban, topic, iplookup, 1337, help, echo [0,1], repeat, wakeup\r\n", packet->sender);
					if ( strcmp(packet->sender, packet->channel) )
						write_to_socket(session, out, "\rPRIVMSG %s :\001ACTION Just PM'd %s the HELP menu.\001\r\n", packet->channel, packet->sender);

				} else if ( !strcmp(command->cmd, "echo") && command->argc >= 1) {

					echo_config(session, packet, out, command, &echoing);
					printf("[*] echoing = %s\n", echoing ? echoing : "nobody");

				} else if ( !strcmp(command->cmd, "repeat") && command->argc >= 1) {

					/* would NOT want it to repeat itself */
					if (strcmp(packet->sender, session->nick)) {
						write_to_socket(session, out, "\rPRIVMSG %s :\"", packet->channel);
						send_args(command->argv, &command->argc, session, out);
						write_to_socket(session, out, "\" -- %s\r\n", packet->sender);
					}

				} else if ( !strcmp(command->cmd, "1337") && command->argc >= 1) {
					char *str = concat_arr(command->argv, &command->argc);

					for (int i = 0, n = strlen(str); i < n; i++) {
						switch( tolower(str[i]) ) {

							case 'i':
							case 'l':
								str[i] = '1';
								break;
							case 'e':
								str[i] = '3';
								break;
							case 'a':
								str[i] = '4';
								break;
							case 's':
								str[i] = '5';
								break;
							case 't':
								str[i] = '7';
								break;
							case 'o':
								str[i] = '0';
								break;
						}
					}

					write_to_socket(session, out, "\rPRIVMSG %s :%s\r\n", packet->channel, str);

					free(str);
				}

				if (admin_is_sender) {

					if (!strcmp(command->cmd, "iplookupset") && command->argc >= 1) {

						if (*(command->argv[0]) == '1') {
							iplookupset = 1;
						} else {
							iplookupset = 0;
						}

						write_to_socket(session, out, "\rPRIVMSG %s :iplookupset = %d\r\n", packet->channel, iplookupset);

					} else if (!strcmp(command->cmd, "join") && command->argc >= 1) {

						if (arr_find(session->channels, command->argv[0], &session->num_channels) != NULL)
							continue;

						arr_push_back(&session->channels, command->argv[0], &session->num_channels);

						join_channel(session);
						printf("[*] Joining %s...\n", command->argv[0]);

					} else if (!strcmp(command->cmd, "part") && command->argc >= 1) {

						if (arr_find(session->channels, command->argv[0], &session->num_channels) == NULL)
							continue;

						write_to_socket(session, out, "\rPART %s\r\n", command->argv[0]);
						printf("[*] Parting from %s...\n", command->argv[0]);

						arr_remove(&session->channels, command->argv[0], &session->num_channels);

					} else if (!strcmp(command->cmd, "nick") && command->argc >= 1) {

						if (session->nick != NULL) free(session->nick);
						session->nick = strdup(command->argv[0]);

						write_to_socket(session, out, "\rNICK %s\r\n", session->nick);
						printf("[*] Changing nick to %s...\n", session->nick);

					} else if (!strcmp(command->cmd, "quit")) {

						write_to_socket(session, out, "\rQUIT :Quit: ");
						if (command->argc != 0) {
							send_args(command->argv, &command->argc, session, out);
						} else {
							write_to_socket(session, out, "Leaving.");
						}
						write_to_socket(session, out, "\r\n");

						sleep(3);
						break;

					} else if (!strcmp(command->cmd, "restart")) {

						write_to_socket(session, out, "\rQUIT :Quit: Restarting\r\n");

						restart = 1;
						break;

					} else if ( !strcmp(command->cmd, "kick")  && command->argc >= 1) {

						write_to_socket(session, out, "\rKICK %s %s\r\n", packet->channel, command->argv[0]);

					} else if ( !strcmp(command->cmd, "ignore") && command->argc >= 1) {

						if ( arr_find(session->admins, command->argv[0], &session->num_admins) ) {
							printf("[*] %s tried to ignore an admin (%s)\n", packet->sender, command->argv[0]);
							continue;
						}

						if ( arr_find(session->ignoring, command->argv[0], &session->num_ignoring) ) {
							printf("[*] %s is already being ignored.\n", command->argv[0]);
							continue;
						}

						printf("[*] Ignore command triggered.\n");
						arr_push_back(&session->ignoring, command->argv[0], &session->num_ignoring);
						printf("[*] Now ignoring %s.\n", command->argv[0]);

					} else if ( !strcmp(command->cmd, "unignore") && command->argc >= 1) {

						if ( arr_find(session->ignoring, command->argv[0], &session->num_ignoring) == NULL) {
							continue;
						}

						arr_remove(&session->ignoring, command->argv[0], &session->num_ignoring);
						printf("[*] No longer ignoring %s.\n", command->argv[0]);

					} else if ( !strcmp(command->cmd, "addadmin") && command->argc >= 1) {

						if ( arr_find(session->admins, command->argv[0], &session->num_admins)  ) {
							printf("[*] %s is already an admin.\n", command->argv[0]);
							continue;
						}

						arr_push_back(&session->admins, command->argv[0], &session->num_admins);
						printf("[*] %s is now an admin.\n", command->argv[0]);

					} else if ( !strcmp(command->cmd, "sleep") ) {

						sleeping = 1;

						write_to_socket(session, out, "\rPRIVMSG %s :I'm tired. KTHXBAI\r\n", packet->channel);

					} else if ( !strcmp(command->cmd, "send") && command->argc >= 2) {
						/* check for channel and check if bot is in channel. */
						if ( arr_find( session->channels , command->argv[0] , &session->num_channels ) ) {

							size_t tmp_len = command->argc - 1;
							char **tmp_arr = command->argv + 1;
							char *msg = concat_arr( tmp_arr, &tmp_len );

							write_to_socket(session, out, "\rPRIVMSG %s :%s\r\n", command->argv[0], msg);
							free(msg);
						} else {
							write_to_socket(session, out, "\rPRIVMSG %s :I am not in the channel %s. Ask me to join?\r\n", packet->sender, command->argv[0]);
						}
					}
				}
				arr_free(&command->argv, &command->argc);
			}

			if (!strcmp(packet->type, "JOIN")) {

				if (strcmp(packet->sender, session->nick) != 0) {

					char* host;
					if ((host = parse_for_host(packet)) != NULL && iplookupset) {
						printf("[*] host = %s\n", host);
						ip_lookup(host, out, session, packet);
					}

				} else {
					write_to_socket(session, out, "\rPRIVMSG %s :Hi everybody!\r\n", packet->channel);
				}
			}

			if (!strcmp(packet->type, "KICK") && strstr(packet->content, session->nick)) {

				printf("[*] Got kicked from %s\n", packet->channel);
				sleep(0.4);

				char* loc = arr_find(session->channels, packet->channel, &session->num_channels);

				if (loc == NULL) {
					continue;
				}

				// swap if channel kicked from isn't last channel.
				if (loc != session->channels[ session->num_channels - 1 ] ) {
					char* tmp = loc;
					loc = session->channels[ session->num_channels - 1 ];
					session->channels[ session->num_channels - 1 ] = tmp;
				}

				join_channel(session);
				write_to_socket(session, out, "\rPRIVMSG %s :You tried, boiiii!\r\n", packet->channel);

			}

			/*
				TODO: Be able to update ignore list if user changes nick.
			*/
			if ( !strcmp(packet->type, "NICK") ) {
				char *find_admin, *find_ignore;

				find_admin = arr_find(session->admins, packet->sender, &session->num_admins);
				find_ignore = arr_find(session->ignoring, packet->sender, &session->num_ignoring);

				if (find_admin) {
					free(find_admin);
					find_admin = strdup(packet->channel);
				}

				if (find_ignore) {
					free(find_ignore);
					find_ignore = strdup(packet->channel);
				}

				printf("[*] %s changed his nick to %s. Updating admin/ignore lists if needed...\n", packet->sender, packet->channel);
			}

			memset(buf, 0, BUFFER_SIZE);
		}

		if (echoing != NULL)
			free(echoing);

		free(buf);
		free(out);

		free(packet);
		close(session->sockfd);

		if (restart) {
			printf("[*] Restarting...\n\r");
			for (int i = 0; i < 4; i++) {
				printf(" ### ");
				sleep(0.5);
			}
			printf("\n");
		}
	}

	free_session(session);

	return 0;
}
