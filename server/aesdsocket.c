#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/queue.h>
#include <fcntl.h>
#include <unistd.h>

#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>
#include <sys/ioctl.h>

#include "time_functions_shared.h"
#include "../aesd-char-driver/aesd_ioctl.h"

#define USE_AESD_CHAR_DEVICE

#ifdef USE_AESD_CHAR_DEVICE
#define OUTPUT_FILE "/dev/aesdchar"
#else
#define OUTPUT_FILE "/var/tmp/aesdsocketdata"
#endif

typedef struct {
    bool sig_rec;
    int s_fd;
    int recv_s_fd;
    int fd;
    struct sockaddr_storage address;
    pthread_mutex_t * t_mutex;
}AESDSOC_DATA_t;

typedef struct {
    AESDSOC_DATA_t aesd_data;
    bool thread_complete;
    pthread_t thread;
}aesdsocket_list_data_t;

struct aesdsoc_linked_list_s{
    aesdsocket_list_data_t data;
    SLIST_ENTRY(aesdsoc_linked_list_s) entries;
};

typedef struct aesdsoc_linked_list_s aesdsoc_linked_list_t;

/* Global Variables*/

static AESDSOC_DATA_t aesdsocketData = {
    .sig_rec = false,
    .s_fd = 0,
    .recv_s_fd = 0,
    .fd = 0,
    .t_mutex = NULL
};




/*********** LOCAL FUNCTIONS ***********/
static void _aesdsocket_handler (int signum)
{
    if(signum == SIGINT || signum == SIGTERM )
    {
        aesdsocketData.sig_rec = true;
    }   
    else
    {
        /* Continue */
    }

    return;
}

static void _aesdsocket_shutdownHelper(int recv_s_fd)
{
    if(recv_s_fd > 0)
    {
        shutdown(recv_s_fd, SHUT_RD);
        close(recv_s_fd);
    }
    else
    {
        /* Continue */
    }

    if(aesdsocketData.s_fd > 0)
    {
        shutdown(aesdsocketData.s_fd, SHUT_RDWR);
        close(aesdsocketData.s_fd);
    }
    else
    {
        /*continue*/
    }

    pthread_mutex_destroy(aesdsocketData.t_mutex);

    return;
}

#ifndef USE_AESD_CHAR_DEVICE
static bool setup_timer( int clock_id,
                         timer_t timerid)
{
    bool success = false;
    struct timespec start_time;

    if ( clock_gettime(clock_id, &start_time) != 0 ) {
        printf("Error %d (%s) getting clock %d time\n",errno,strerror(errno),clock_id);
        (void) start_time;
    } else {
        struct itimerspec itimerspec;
        
        memset(&itimerspec, 0, sizeof(struct itimerspec));
        itimerspec.it_interval.tv_sec = 10;
        itimerspec.it_interval.tv_nsec = 0;
        timespec_add(&itimerspec.it_value, &start_time,&itimerspec.it_interval);
        if( timer_settime(timerid, TIMER_ABSTIME, &itimerspec, NULL ) != 0 ) {
            printf("Error %d (%s) setting timer\n",errno,strerror(errno));
        } else {
            success = true;
        }
    }

    (void)start_time;
    return success;
}
#endif

static int _aesdsocket_Init(void)
{
    int status;
    struct sigaction l_sig;
    struct addrinfo hints;
    struct addrinfo *servinfo;

    /* Set up signal handler */
    memset(&l_sig, 0, sizeof(l_sig));
    l_sig.sa_handler = _aesdsocket_handler;
    l_sig.sa_flags = 0;
    sigfillset(&l_sig.sa_mask);

    if (sigaction(SIGINT , (const struct sigaction *)&l_sig, NULL) != 0)
    {
        syslog(LOG_ERR, "signal handler for SIGINT failed to set with err, %d", errno);
        return -1;
    }
    else
    {
        /* continue */
    }

    if (sigaction(SIGTERM , (const struct sigaction *)&l_sig, NULL) != 0)
    {
        syslog(LOG_ERR, "signal handler for SIGINT failed to set with err, %d", errno);
        return -1;
    }
    else
    {
        /* continue */
    }



    /* initialize socket at port 9000 */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if((status = getaddrinfo(NULL, "9000", &hints, &servinfo)) != 0)
    {
        syslog(LOG_ERR, "getaddrinfo error: %s", gai_strerror(status));
        return -1;
    }
    else
    {
        /* continue */
    }

    if((aesdsocketData.s_fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1)
    {
        syslog(LOG_ERR, "socket() error");
        freeaddrinfo(servinfo);
        return -1;
    }
    else
    {
        /* continue */
    }

    if(bind(aesdsocketData.s_fd, servinfo->ai_addr, servinfo->ai_addrlen) == -1)
    {
        syslog(LOG_ERR, "bind() error: %d", errno);
        freeaddrinfo(servinfo);
        return -1;
    }
    
    freeaddrinfo(servinfo);
    return 0;
}

#ifndef USE_AESD_CHAR_DEVICE
static void _aesdsocket_update_timestamp_thread(union sigval sig_val)
{
    aesdsocket_list_data_t * thread_data = (aesdsocket_list_data_t *) sig_val.sival_ptr;
    struct timespec current_time;
    struct tm * tm_current;
    size_t written;
    char time_str[100];
    const char newline_str[2] = {'\n', 0};
    
    if ( clock_gettime(CLOCK_REALTIME, &current_time) != 0 ) {
        printf("Error %d (%s) getting clock %d time\n",errno,strerror(errno),CLOCK_REALTIME);
        (void) current_time;
        return;
    } else {
        /* Continue */
        (void) current_time;
    }

    if((tm_current = localtime(&current_time.tv_sec)) == NULL)
    {
        printf("Error %d (%s) getting local time\n",errno,strerror(errno));
        return;
    }
    else{
        /* continue */
    }

    if(strftime(time_str, sizeof(time_str), "timestamp:%Y %b %d %T", tm_current) == 0)
    {
        printf("Error %d (%s) converting time to string\n",errno,strerror(errno));
        return;
    }
    else
    {
        /* Continue */
        strcat(time_str, newline_str);
    }

    if ( pthread_mutex_lock(thread_data->aesd_data.t_mutex) != 0 ) {
        printf("Error %d (%s) locking thread data!\n",errno,strerror(errno));
        return;
    } else {
        
        written = write(thread_data->aesd_data.fd, time_str, strlen(time_str));

        if ( pthread_mutex_unlock(thread_data->aesd_data.t_mutex) != 0 ) {
            printf("Error %d (%s) unlocking thread data!\n",errno,strerror(errno));
            return;
        }

        if (written == -1) {
            syslog(LOG_ERR, "write() error: %d", errno);
            return;
        }
        else
        {
            /* Continue */
        }
    }

    return;
}
#endif

static void * _aesdsocket_thread_fn(void* thread_param)
{
    size_t rd_len = 0;
    char rd_buff[1024];
    char *wr_buff = NULL;
    char *new_wr_buff = NULL;
    char *cmd_str = NULL;
    struct aesd_seekto seekto;
    ssize_t sent_bytes = 0, written, total_read = 0;
    aesdsocket_list_data_t * thread_data = (aesdsocket_list_data_t *)thread_param;
    void *addr;
    char ipstr[INET6_ADDRSTRLEN];

   if(thread_param !=NULL)
   {
    
        if (thread_data->aesd_data.address.ss_family == AF_INET) 
        { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)&thread_data->aesd_data.address;
            addr = &(ipv4->sin_addr);
        } 
        else 
        { // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&thread_data->aesd_data.address;
            addr = &(ipv6->sin6_addr);
        }

        // convert the IP to a string and print it:
        inet_ntop(thread_data->aesd_data.address.ss_family, addr, ipstr, sizeof(ipstr));
        syslog(LOG_INFO, "Accepted connection from %s", ipstr);
        memset(rd_buff, 0, sizeof(rd_buff));
        
        do
        {
            if((rd_len = recv(thread_data->aesd_data.recv_s_fd, (void *)rd_buff, (sizeof(rd_buff) -1), 0)) == -1)
            {
                if((errno == EINTR) && (thread_data->aesd_data.sig_rec != false))
                {
                    syslog(LOG_INFO, "Caught signal, exiting");
                    shutdown(thread_data->aesd_data.recv_s_fd, SHUT_RD);
                    close(thread_data->aesd_data.recv_s_fd);
                    thread_data->thread_complete = true;
                    return thread_data;
                }
                else
                {
                    syslog(LOG_ERR, "recv() error: %d", errno);
                    shutdown(thread_data->aesd_data.recv_s_fd, SHUT_RD);
                    close(thread_data->aesd_data.recv_s_fd);
                    thread_data->thread_complete = true;
                    return thread_data;
                }
            }
            else if (rd_len == 0)
            {
                break;
            }
            else
            {      
				syslog(LOG_INFO, "From: %d, Received: %s", thread_data->aesd_data.recv_s_fd, rd_buff);
				 
				if (pthread_mutex_lock(thread_data->aesd_data.t_mutex) != 0) 
				{
					syslog(LOG_ERR, "pthread_mutex_lock() error");
					break;
				} 

                thread_data->aesd_data.fd = open(OUTPUT_FILE, O_RDWR|O_CREAT|O_APPEND, 0766);
                if(thread_data->aesd_data.fd == -1)
                {
                    pthread_mutex_unlock(thread_data->aesd_data.t_mutex);
                    syslog(LOG_ERR, "open() error: %d", errno);
                    shutdown(thread_data->aesd_data.recv_s_fd, SHUT_RD);
                    close(thread_data->aesd_data.recv_s_fd);
                    thread_data->thread_complete = true;
                    return thread_data;
                }
                else
                {
                    /* continue*/
                    syslog(LOG_INFO, "Opened fd: %d", thread_data->aesd_data.fd);
                }
              
				if(wr_buff != NULL)
				{
					new_wr_buff = realloc(wr_buff, (strlen(wr_buff) + rd_len + 1));
					memset ((new_wr_buff + strlen(wr_buff)), 0, (rd_len + 1));
				}
				else
				{
					new_wr_buff = realloc(wr_buff, (rd_len + 1));
					memset (new_wr_buff, 0, (rd_len + 1));
				}
                
                if(new_wr_buff != NULL)
                {
					wr_buff = new_wr_buff;
					strcat(wr_buff, rd_buff);
					if(wr_buff[strlen(wr_buff) - 1u] == '\n')
					{
						if((cmd_str = strstr(wr_buff, "AESDCHAR_IOCSEEKTO:")) != NULL)
						{
							cmd_str += 19;
							if((cmd_str = strtok((char *) cmd_str, ",")) != NULL)
							{
								seekto.write_cmd = (uint32_t)atoi(cmd_str);
								if((cmd_str = strtok(NULL, ",")) != NULL)
								{
									seekto.write_cmd_offset = (uint32_t)atoi(cmd_str);									
								}
								else
								{
									pthread_mutex_unlock(thread_data->aesd_data.t_mutex);
									free(wr_buff);
									shutdown(thread_data->aesd_data.recv_s_fd, SHUT_RD);
									close(thread_data->aesd_data.recv_s_fd);
									close(thread_data->aesd_data.fd);
#ifndef USE_AESD_CHAR_DEVICE    
									unlink(OUTPUT_FILE);
#endif
									thread_data->thread_complete = true;
									return thread_data;
								}
								
								if(ioctl(thread_data->aesd_data.fd, AESDCHAR_IOCSEEKTO , &seekto) < 0)
								{
									pthread_mutex_unlock(thread_data->aesd_data.t_mutex);
									free(wr_buff);
									syslog(LOG_ERR, "ioctl() error: %d", errno);
									shutdown(thread_data->aesd_data.recv_s_fd, SHUT_RD);
									close(thread_data->aesd_data.recv_s_fd);
									close(thread_data->aesd_data.fd);
#ifndef USE_AESD_CHAR_DEVICE    
									unlink(OUTPUT_FILE);
#endif
									thread_data->thread_complete = true;
									return thread_data;
								}
								else
								{
									syslog(LOG_INFO, "ioctl() completed successfully, cmd: %u, cmd_offset: %u", seekto.write_cmd, seekto.write_cmd_offset);
									free(wr_buff);
									wr_buff = NULL;
								}
							}
							else
							{
								pthread_mutex_unlock(thread_data->aesd_data.t_mutex);
								free(wr_buff);
								shutdown(thread_data->aesd_data.recv_s_fd, SHUT_RD);
								close(thread_data->aesd_data.recv_s_fd);
								close(thread_data->aesd_data.fd);
#ifndef USE_AESD_CHAR_DEVICE    
								unlink(OUTPUT_FILE);
#endif
								thread_data->thread_complete = true;
								return thread_data;
							}
						}
						else if((written = write(thread_data->aesd_data.fd, wr_buff, strlen(wr_buff))) != strlen(wr_buff))
						{
							if (written == -1) {
								pthread_mutex_unlock(thread_data->aesd_data.t_mutex);
								free(wr_buff);
								syslog(LOG_ERR, "write() error: %d", errno);
								shutdown(thread_data->aesd_data.recv_s_fd, SHUT_RD);
								close(thread_data->aesd_data.recv_s_fd);
								close(thread_data->aesd_data.fd);
#ifndef USE_AESD_CHAR_DEVICE    
								unlink(OUTPUT_FILE);
#endif
								thread_data->thread_complete = true;
								return thread_data;
							}
						}
						else
						{
							/* Continue */
							syslog(LOG_INFO, "From: %d, wrote: %s to fd: %d", thread_data->aesd_data.recv_s_fd, wr_buff, thread_data->aesd_data.fd);
							free(wr_buff);
							wr_buff = NULL;
#ifndef USE_AESD_CHAR_DEVICE
							fsync(thread_data->aesd_data.fd);  // Ensure data is written to disk
#endif
							lseek(thread_data->aesd_data.fd, 0, SEEK_SET); // Go to the start of the file
						}
					}
					else
					{
						
						continue;
					}
				}
				else
				{
					free(wr_buff);
					shutdown(thread_data->aesd_data.recv_s_fd, SHUT_RD);
					close(thread_data->aesd_data.recv_s_fd);
					close(thread_data->aesd_data.fd);
#ifndef USE_AESD_CHAR_DEVICE    
                        unlink(OUTPUT_FILE);
#endif
					thread_data->thread_complete = true;
					return thread_data;
				}

				memset(rd_buff, 0, sizeof(rd_buff));
				
				syslog(LOG_INFO, "Output Stream attempting on fd: %d", thread_data->aesd_data.fd);

				while ((total_read = read(thread_data->aesd_data.fd, rd_buff, sizeof(rd_buff))) > 0) 
				{
					sent_bytes = 0;
					while (sent_bytes < total_read) 
					{
						syslog(LOG_INFO, "%s", &rd_buff[sent_bytes]);
						written = send(thread_data->aesd_data.recv_s_fd, rd_buff + sent_bytes, total_read - sent_bytes, 0);
						if (written == -1) 
						{
							syslog(LOG_ERR, "send() error: %d", errno);
							pthread_mutex_unlock(thread_data->aesd_data.t_mutex);
							free(wr_buff);
							shutdown(thread_data->aesd_data.recv_s_fd, SHUT_RD);
							close(thread_data->aesd_data.recv_s_fd);
							close(thread_data->aesd_data.fd);
#ifndef USE_AESD_CHAR_DEVICE    
					unlink(OUTPUT_FILE);
#endif
							thread_data->thread_complete = true;
							return thread_data;
						}
						sent_bytes += written;
					}
				}
				if (total_read == -1) {
					syslog(LOG_ERR, "read() error: %d", errno);
					pthread_mutex_unlock(thread_data->aesd_data.t_mutex);
					free(wr_buff);
					shutdown(thread_data->aesd_data.recv_s_fd, SHUT_RD);
					close(thread_data->aesd_data.recv_s_fd);
					close(thread_data->aesd_data.fd);
#ifndef USE_AESD_CHAR_DEVICE    
					unlink(OUTPUT_FILE);
#endif
					thread_data->thread_complete = true;
					return thread_data;
				}
				
				
				memset(rd_buff, 0, sizeof(rd_buff));

                close(thread_data->aesd_data.fd);
#ifndef USE_AESD_CHAR_DEVICE    
                unlink(OUTPUT_FILE);
#endif
                if (pthread_mutex_unlock(thread_data->aesd_data.t_mutex) != 0) 
                {
					syslog(LOG_ERR, "pthread_mutex_unlock() error");
					break;
				}              
            } 
            
        }while(rd_len != 0);
        
        pthread_mutex_unlock(thread_data->aesd_data.t_mutex);
		free(wr_buff);
        shutdown(thread_data->aesd_data.recv_s_fd, SHUT_RD);
        close(thread_data->aesd_data.recv_s_fd);

        syslog(LOG_INFO, "Closed connection from %s", ipstr);
   }
    
    thread_data->thread_complete = true;
    return thread_data;
}

static int _aesdsocket_Run(void)
{
    socklen_t addrlen;
    pthread_mutex_t t_mutex;
    aesdsoc_linked_list_t * thread_list_entry = NULL, *temp_entry = NULL;

    pthread_mutex_init(&t_mutex, NULL);
    aesdsocketData.t_mutex = &t_mutex;

    if(listen(aesdsocketData.s_fd, 8) == -1)
    {
        syslog(LOG_ERR, "listen() error: %d", errno);
        shutdown(aesdsocketData.s_fd, SHUT_RDWR);
        close(aesdsocketData.s_fd);
        pthread_mutex_destroy(aesdsocketData.t_mutex);
        return -1;
    }
    else
    {
#ifndef USE_AESD_CHAR_DEVICE

        struct sigevent sev;
        timer_t timerid;
        /*Setup the timer thread */
        memset(&sev,0,sizeof(struct sigevent));
        /**
        * Setup a call to timer_thread passing in the td structure as the sigev_value
        * argument
        */
        sev.sigev_notify = SIGEV_THREAD;
        sev.sigev_value.sival_ptr = &aesdsocketData;
        sev.sigev_notify_function = _aesdsocket_update_timestamp_thread;

        if ( timer_create(CLOCK_MONOTONIC,&sev,&timerid) != 0 ) {
            printf("Error %d (%s) creating timer!\n",errno,strerror(errno));
            shutdown(aesdsocketData.s_fd, SHUT_RDWR);
            close(aesdsocketData.s_fd);
            pthread_mutex_destroy(aesdsocketData.t_mutex);
            return 1;
        } 
        else 
        {
            if(setup_timer(CLOCK_MONOTONIC, timerid) != true)
            {
                printf("Error %d (%s) creating timer!\n",errno,strerror(errno));
                shutdown(aesdsocketData.s_fd, SHUT_RDWR);
                close(aesdsocketData.s_fd);
                pthread_mutex_destroy(aesdsocketData.t_mutex);
                return 1;
            }
            else
            {
                /* continue */
            }
        }
#endif

        /* Initialized the head of the linked list */
        SLIST_HEAD(slisthead, aesdsoc_linked_list_s) head;
        SLIST_INIT(&head);
        
        while(1)
        {
            memset(&aesdsocketData.address, 0, sizeof(aesdsocketData.address));
            addrlen = sizeof(aesdsocketData.address);
            if((aesdsocketData.recv_s_fd = accept(aesdsocketData.s_fd, (struct sockaddr *)&aesdsocketData.address, &addrlen)) == -1)
            {
                if((errno == EINTR) && (aesdsocketData.sig_rec != false))
                {
                    syslog(LOG_INFO, "Caught signal, exiting");
                    
                    /* Close Thread List */
                    while (!SLIST_EMPTY(&head)) {
                        thread_list_entry = SLIST_FIRST(&head);
                        (void)pthread_join(thread_list_entry->data.thread, NULL);
                        SLIST_REMOVE_HEAD(&head, entries);
                        free(thread_list_entry);
                    }

                    _aesdsocket_shutdownHelper(aesdsocketData.recv_s_fd);
#ifndef USE_AESD_CHAR_DEVICE
                    if (timer_delete(timerid) == -1) 
                    {
                        perror("timer_delete");
                    }
#endif
                    return 0;
                }
                else
                {
                    syslog(LOG_ERR, "accept() error: %d", errno);

                    /* Close Thread List */
                    while (!SLIST_EMPTY(&head)) {
                        thread_list_entry = SLIST_FIRST(&head);
                        (void)pthread_join(thread_list_entry->data.thread, NULL);
                        SLIST_REMOVE_HEAD(&head, entries);
                        free(thread_list_entry);
                    }

                    _aesdsocket_shutdownHelper(aesdsocketData.recv_s_fd);
#ifndef USE_AESD_CHAR_DEVICE
                    if (timer_delete(timerid) == -1) 
                    {
                        perror("timer_delete");
                    }
#endif
    		     
                    return -1;
                }
                
            }
            else
            {
                thread_list_entry = malloc(sizeof(aesdsoc_linked_list_t));
                if(thread_list_entry != NULL)
                {
                    memcpy(&thread_list_entry->data.aesd_data, &aesdsocketData, sizeof(AESDSOC_DATA_t));
                    thread_list_entry->data.thread_complete = false;

                    if(pthread_create(&thread_list_entry->data.thread, NULL, _aesdsocket_thread_fn, &thread_list_entry->data) != 0)
                    {
                        syslog(LOG_INFO, "pthread_create failed");
                        free(thread_list_entry);

                        /* Close Thread List */
                        while (!SLIST_EMPTY(&head)) {
                            thread_list_entry = SLIST_FIRST(&head);
                            (void)pthread_join(thread_list_entry->data.thread, NULL);
                            SLIST_REMOVE_HEAD(&head, entries);
                            free(thread_list_entry);
                        }

                        _aesdsocket_shutdownHelper(aesdsocketData.recv_s_fd);
#ifndef USE_AESD_CHAR_DEVICE
                        if (timer_delete(timerid) == -1) 
                    	{
        			        perror("timer_delete");
    		     	    }
#endif
    		     
                        return -1;
                    }
                    else
                    {
                        syslog(LOG_INFO, "Thread created: %lu, s_fd: %d", (unsigned long)thread_list_entry->data.thread, thread_list_entry->data.aesd_data.recv_s_fd);
                        SLIST_INSERT_HEAD(&head, thread_list_entry, entries);
                        /* Continue */
                    }
                }
                else
                {
                    syslog(LOG_ERR, "malloc() error");
                    
                    /* Close Thread List */
                    while (!SLIST_EMPTY(&head)) {
                        thread_list_entry = SLIST_FIRST(&head);
                        (void)pthread_join(thread_list_entry->data.thread, NULL);
                        SLIST_REMOVE_HEAD(&head, entries);
                        free(thread_list_entry);
                    }

                    _aesdsocket_shutdownHelper(aesdsocketData.recv_s_fd);
#ifndef USE_AESD_CHAR_DEVICE
                    if (timer_delete(timerid) == -1) 
                    {
                        perror("timer_delete");
                    }
#endif
                    return -1;
                }
            }

            for(thread_list_entry = SLIST_FIRST(&head); thread_list_entry != NULL; thread_list_entry = temp_entry)
            {
                temp_entry = SLIST_NEXT(thread_list_entry, entries);
                if(thread_list_entry->data.thread_complete != false)
                {
                    syslog(LOG_INFO, "Thread completed: %lu, s_fd: %d", (unsigned long)thread_list_entry->data.thread, thread_list_entry->data.aesd_data.recv_s_fd);
                    (void)pthread_join(thread_list_entry->data.thread, NULL);
                    SLIST_REMOVE(&head, thread_list_entry, aesdsoc_linked_list_s, entries);

                    free(thread_list_entry);
                }
                else
                {
                    /* continue */
                }
            }

        }

        // Free remaining elements
        while (!SLIST_EMPTY(&head)) 
        {
        	thread_list_entry = SLIST_FIRST(&head);
            (void)pthread_join(thread_list_entry->data.thread, NULL);
        	SLIST_REMOVE_HEAD(&head, entries);
        	free(thread_list_entry);
    	}
#ifndef USE_AESD_CHAR_DEVICE
    	if (timer_delete(timerid) == -1) 
        {
        	perror("timer_delete");
        	return -1;
    	}
#endif
    }

    shutdown(aesdsocketData.s_fd, SHUT_RDWR);
    close(aesdsocketData.s_fd);
    pthread_mutex_destroy(aesdsocketData.t_mutex);

    return 0;
}



/********GLOBAL FUNCTIONS *************/
int main(int num_args, char * option[])
{
    bool daemon = false;

    openlog(NULL, 0, LOG_USER);

    if(_aesdsocket_Init() != 0)
    {
        syslog(LOG_ERR, "aesdsocket failed to initialize");
        return -1;
    }
    else
    {
        /* continue */
    }

    if(num_args == 2)
    {
        if(strstr(option[1], "-d") != NULL)
        {
            syslog(LOG_INFO, "starting aesdsocket in daemon mode");
            daemon = true;
        }
        else
        {
            syslog(LOG_ERR, "%s option not recognized use -d to start in daemon mode or leave blank to run normally", option[1]);
            return 1;
        }
        
    }
    else
    {
        syslog(LOG_INFO, "starting aesdsocket");
    }

    if(daemon != false)
    {
        pid_t pid=fork();
        switch(pid)
        {
            case -1:
            {
                perror("fork");
                return -1;
            }
            case 0:
            {
                /* Child code */
                if(_aesdsocket_Run() != 0)
                {
                    return -1;
                }
                else
                {
                    /* continue */
                }
                break;
            }
            default:
            {
                /* Parent code, Continue*/
                break;
            }
        }
    }
    else
    {
        if(_aesdsocket_Run() != 0)
        {
            return -1;
        }
        else
        {
            /* continue */
        }
    }


    return 0;
}

