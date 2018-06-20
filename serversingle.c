/*
** server.c -- a stream socket server demo
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>

#define PORT "6666"  // the port users will be connecting to

#define BACKLOG 5 // how many pending connections queue will hold

static char* next;
static char* next_arg;
static char* args[512];
static char* digi;
static int next_count;
static int cmd_no[30];
static char* ptr_env;
static int _break=0;
static int first;
static char* filename;
static int client_pipe[30][30]; //1>2 would save to client_pipe[0][1].
static int to_other=0;

struct clientdata{
	int fd;
	char name[20];
	char ip[15];
	int port;
	char path[50]; //their own environment.
};

static int total_cli=0;

static struct clientdata clients[30];

void addtotable(int fd){
	for (int i=0;i<30;i++){
		if(clients[i].fd==0){
			clients[i].fd=fd;
			strcpy(clients[i].name,"(no name)");
			strcpy(clients[i].ip,"CGILAB");
			clients[i].port=511;
			strcpy(clients[i].path,"bin:.");
			total_cli++;
			break;
		}
	}
}

int fdtoindex(int fd);
void delfromtable(int fd){
	for (int i=0;i<30;i++){
		if(clients[i].fd==fd){
			clients[i].fd=0;
			strcpy(clients[i].name,"(no name)");
			strcpy(clients[i].ip,"123.123");
			clients[i].port=123;
			bzero(clients[i].path,50);
			cmd_no[fdtoindex(fd)]=0;
			total_cli--;
			break;
		}
	}
}

int fdtoindex(int fd){
	for (int i=0;i<30;i++){
		if(clients[i].fd==fd){
			return i;
		}
	}
}

void who(int fd){
	int total=0;
	int i=0;
	char mes[10000];
	sprintf(mes,"<ID>\t<nickname>\t<IP/port>\t<indicate me>\n");
	send(fd,mes,strlen(mes),0);
	while(total!=total_cli){
		if(clients[i].fd!=0){
			total++;
			if(clients[i].fd==fd){
				sprintf(mes,"%d\t%s\t%s/%d\t<-me\n",(i+1),clients[i].name,clients[i].ip,clients[i].port);
				send(fd,mes,strlen(mes),0);
			}
			else
			{
				sprintf(mes,"%d\t%s\t%s/%d\n",(i+1),clients[i].name,clients[i].ip,clients[i].port);
				send(fd,mes,strlen(mes),0);
			}
		}
		i++;
	}
}

void sendsym(int fd){
	char *sym="% ";
	send(fd,sym,strlen(sym),0);
}


void sigchld_handler(int s)
{
	(void)s; // quiet unused variable warning

	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void split(char* cmd);
void run(int next_count,int me);
static int ispipe(char* cmd);
static int fd[30][1024][2];
static int tofile(char *args[]);
static int fromwho(void);

static int pipein_num(int* pipein);
static fd_set master;    // master file descriptor list
static fd_set read_fds;  // temp file descriptor list for select()
static int fdmax;        // maximum file descriptor number
static char thecmd[10000]; //init the command

static char* other;
static int towho;
int target(char* cmd);

static int listener;     // listening socket descriptor
static int newfd;        // newly accept()ed socket descriptor

void cmdnonr(char* cmd);

void broadcast(char* mes,int fd){
	for(int j = 0; j <= fdmax; j++) {
		// send to everyone!
		if (FD_ISSET(j, &master)) {
			// except the listener and ourselves
			// if (j != listener && j != fd) {

			if (j != listener) {
				send(j,mes,strlen(mes),0);
				// if (send(j, buf, nbytes, 0) == -1) {
					// perror("send");
				// }
			}
		}
	}
}

void name(char *name,int fd){
	int total=0;
	int i=0;
	int used=0;
	while(total!=total_cli){
		if(clients[i].fd!=0){
			total++;
			if(strcmp(name, clients[i].name) == 0){
				used=1;
				char mes[10000];
				sprintf(mes,"*** User '%s' already exists. ***\n",name);
				send(fd,mes,strlen(mes),0);
				break;
			}
		}
		i++;
	}
	if (!used){
		strcpy(clients[fdtoindex(fd)].name,args[1]);
		char mes[10000];
		sprintf(mes,"*** User from %s/%d is named '%s'. ***\n",clients[fdtoindex(fd)].ip,clients[fdtoindex(fd)].port,clients[fdtoindex(fd)].name);
		broadcast(mes,fd);
	}	
}
int main(void)
{
	// setenv("PATH","bin:.",1);
	struct sockaddr_storage remoteaddr; // client address
	socklen_t addrlen;

	char buf[256];    // buffer for client data
	int nbytes;

	char remoteIP[INET6_ADDRSTRLEN];

	int yes=1;        // for setsockopt() SO_REUSEADDR, below
	int i, j, rv;

	struct addrinfo hints, *ai, *p;

	FD_ZERO(&master);    // clear the master and temp sets
	FD_ZERO(&read_fds);


	// get us a socket and bind it
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
		fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
		exit(1);
	}

	for(p = ai; p != NULL; p = p->ai_next) {
		listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (listener < 0) { 
		continue;
	}

	// lose the pesky "address already in use" error message
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

	if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
		close(listener);
		continue;
	}
	break;
	}

	// if we got here, it means we didn't get bound
	if (p == NULL) {
		fprintf(stderr, "selectserver: failed to bind\n");
		exit(2);
	}

	freeaddrinfo(ai); // all done with this

	// listen
	if (listen(listener, 10) == -1) {
		perror("listen");
		exit(3);
	}

	// add the listener to the master set
	FD_SET(listener, &master);

	// keep track of the biggest file descriptor
	fdmax = listener; // so far, it's this one

	// main loop
	for(;;) {
		read_fds = master; // copy it
		if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
			perror("select");
			exit(4);
		}

		// run through the existing connections looking for data to read
		for(i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &read_fds)) { // we got one!!
				if (i == listener) {
					// handle new connections
					addrlen = sizeof remoteaddr;
					newfd = accept(listener,(struct sockaddr *)&remoteaddr,&addrlen);

					if (newfd == -1) {
						perror("accept");
					} else {
						addtotable(newfd);
						FD_SET(newfd, &master); // add to master set
						if (newfd > fdmax) {    // keep track of the max
							fdmax = newfd;
						}
						printf("selectserver: new connection from %s on socket %d\n",
						inet_ntop(remoteaddr.ss_family,get_in_addr((struct sockaddr*)&remoteaddr),
						remoteIP, INET6_ADDRSTRLEN),newfd);
						char *welcome="****************************************\n"
						"** Welcome to the information server. **\n"
						"****************************************\n";
						send(newfd,welcome,strlen(welcome),0);
						char mes[10000];
						sprintf(mes,"*** User '%s' entered from %s/%d. ***\n",clients[fdtoindex(newfd)].name,clients[fdtoindex(newfd)].ip,clients[fdtoindex(newfd)].port);
						broadcast(mes,newfd);
						sendsym(newfd);

					}
				} else {
					// handle data from a client
					bzero(buf,10000);
					if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
						// got error or connection closed by client
						if (nbytes == 0) {
							// connection closed
							char mes[10000];
							sprintf(mes,"*** User '%s' left. ***\n",clients[fdtoindex(i)].name);
							broadcast(mes,i);
							for (int j = 0; j < 30; ++j)
							{
								// printf("%d\n",client_pipe[j][fdtoindex(i)]);
								client_pipe[j][fdtoindex(i)]=0;
							}
							delfromtable(i);
							
						} else {
							perror("recv");
						}
						close(i); // bye!
						FD_CLR(i, &master); // remove from master set
					} else {  //parse the input
						// we got some data from a client
						if(buf[0]=='\n' || buf[0]=='\r'){
							;
						}else{
							first=1;
							bzero(thecmd,10000);
							strcpy(thecmd,buf);
							cmdnonr(thecmd);
							to_other=0;
							int saveout=dup(1);
							int saveerr=dup(2);
							dup2(i,1);
							dup2(i,2);
							// printf("123");  //something weird.
							// args[2]=NULL;
							// printf("hahahaha %s\n",args[2]);
							// printf("%d,%d\n",fdtoindex(i),cmd_no[fdtoindex(i)]);
							char* cmd=buf;
							other=strchr(cmd,'>');
							if (other!=NULL) {
								other++;
								if(isdigit(*other)){
									// printf("to other\n");
									to_other=1;
								}else{
									// printf("to file\n");
									;
								}
								other--;
								if(to_other) {
									*other=' ';
									towho=target(other);
								}
								// printf("%d\n",towho);
								// printf("%c\n",other[0]);
							}
							next=strchr(cmd,'|');
							if(next!=NULL) *next='\0';
							while(next!=NULL){
								first=0;
								_break=0;
								// printf("cmd:%s\n",cmd);
								split(cmd);
								// args[1] = NULL;
								// fromwho();
								// printf("args:%s\n",args[0]);
								run(ispipe(next),i);
								++cmd_no[fdtoindex(i)];
								if(_break) break;   //unknow command
								cmd=next;
								next=strchr(cmd,'|');
								if(next==NULL) break;
								*next='\0';
							}
							if(!_break){
								// printf("cmd:%s\n",cmd);
								// printf("bb%s\n",args[30]);
								split(cmd);
								// args[0]=NULL;
								// printf("aa%s\n",args[0]);
								// args[1][0] = 'x';
								// printf("in main function:%d\n",fromwho());
								// printf("args:%s\n",args[1]);
								run(0,i);
								++cmd_no[fdtoindex(i)];
							}
							_break=0;

							dup2(saveout,1);
							dup2(saveerr,2);
							close(saveout);
							close(saveerr);
						}
						sendsym(i);
						// printf("123");  //something weird.
					}
				} // END handle data from client
			} // END got new incoming connection
		} // END looping through file descriptors
	} // END for(;;)--and you thought it would never end!
	return 0;
}
char* skipwhite(char* s)
{
	while (isspace(*s)) ++s;
	return s;
}
void split(char* cmd){
	cmd = skipwhite(cmd);
	next_arg = strchr(cmd,' ');
	int i = 0;
 	args[0]=NULL; // init for yell and yell command.
 	args[1]=NULL;

	while(next_arg != NULL) {
		next_arg[0] = '\0';
		args[i] = cmd;
		++i;
		if(strcmp(args[0], "yell") == 0){
			cmd = skipwhite(next_arg + 1);
			next_arg =NULL;
		}else if(strcmp(args[0],"tell") == 0 && args[1]!=NULL){
			cmd = skipwhite(next_arg + 1);
			next_arg =NULL;
		}else{
			cmd = skipwhite(next_arg + 1);
			next_arg = strchr(cmd, ' ');
		}
	}
	if (cmd[0] != '\0') {
		args[i] = cmd;
		next_arg = strchr(cmd,'\r');
		if(next_arg==NULL) next_arg = strchr(cmd,'\n');
		next_arg[0] = '\0';
		++i; 
	}
	args[i] = NULL;
}

void cmdnonr(char* cmd){
	int i=0;
	while(cmd[i]!='\0'){
		if(cmd[i]=='\n' || cmd[i]=='\r'){
			cmd[i]='\0';
			i--;
		}
		i++;
	}
}


int ispipe(char* cmd){   //it return the pipe number count.
	char num[4];
	for (int i=0;i<4;i++){  // initialize
		num[i]=' ';
	}
	int i=0;
	if(cmd!=NULL){
		digi=cmd+1;
		if(isspace(digi[0])) i=1;
		else{
			while(isdigit(*digi)){
			i++;
			digi++;
			}
			if(i>0){
				strncpy(num,digi-i,i);
				i=atoi(num);
			}
		}
		next=digi;
	}
	return i;
}
int target(char* cmd){   //it return the target id.
	char num[4];
	for (int i=0;i<4;i++){  // initialize
		num[i]=' ';
	}
	int i=0;
	if(cmd!=NULL){
		digi=cmd+1;
		if(isspace(digi[0])) i=1;
		else{
			while(isdigit(*digi)){
			i++;
			digi++;
			}
			if(i>0){
				strncpy(num,digi-i,i);
				digi=digi-i;
				for(int j=0;j<i;j++){
					digi[j]=' ';
				}
				i=atoi(num);
			}
		}
		
	}
	return i;
}

void run(int next_count,int me){
	setenv("PATH",clients[fdtoindex(me)].path,1);
	// printf("%d\n",cmd_no);
	// printf("%s:  to_other:%d,  next_count:%d\n",args[0],to_other,next_count);
	// printf("%d,%d\n",me,fdtoindex(me));
	int inuse=0;
	char mes[10000];
	if(args[0] == NULL){
		--cmd_no[fdtoindex(me)];
	}
	else if(strcmp(args[0], "yell") == 0){
		sprintf(mes,"*** %s yelled ***: %s\n",clients[fdtoindex(me)].name,args[1]);
		broadcast(mes,me);
	}
	else if(strcmp(args[0], "tell") == 0){
		//broadcast!
		if (clients[atoi(args[1])-1].fd==0){
			printf("*** Error: user #%d does not exist yet. ***\n",atoi(args[1]));
		}else{
			sprintf(mes,"*** %s told you ***: %s\n",clients[fdtoindex(me)].name,args[2]);
			send(clients[atoi(args[1])-1].fd,mes,strlen(mes),0);
		}
	}
	else if(strcmp(args[0], "who") == 0){
		who(me);
	}
	else if(strcmp(args[0], "name") == 0){
		name(args[1],me);
	}
	else if(strcmp(args[0], "exit") == 0){
		for (int j = 0; j < 30; ++j)
		{
			// printf("%d\n",client_pipe[j][fdtoindex(me)]);
			client_pipe[j][fdtoindex(me)]=0;
		}
		sprintf(mes,"*** User '%s' left. ***\n",clients[fdtoindex(me)].name);
		broadcast(mes,me);
		delfromtable(me);
		close(me);
		FD_CLR(me, &master);
	}
	else if(strcmp(args[0], "setenv") == 0){
		if(args[1]!=NULL&&args[2]!=NULL) {
			bzero(clients[fdtoindex(me)].path,50);
			strcpy(clients[fdtoindex(me)].path,args[2]);
			// setenv(args[1],args[2],1);
		}
	}else if(strcmp(args[0], "printenv") == 0){
		if(args[1]!=NULL){
			char *ptr_path = getenv(args[1]);
			printf("%s=%s\n",args[1],ptr_path);
		}
	}
	else{
		int self=0;  // 0 is not self, 1 is self.
		if(cmd_no[fdtoindex(me)]>1023) cmd_no[fdtoindex(me)]-=1024;
		if(fd[fdtoindex(me)][cmd_no[fdtoindex(me)]][0]==0) { // check pipe open or not.
			pipe(fd[fdtoindex(me)][cmd_no[fdtoindex(me)]]);
			self=1;
		}
		int next_cmd=cmd_no[fdtoindex(me)]+next_count;
		if(next_cmd>1023) next_cmd-=1024;
		if(fd[fdtoindex(me)][next_cmd][0]!=0) inuse=1;
		if((next_count!=0) && (fd[fdtoindex(me)][next_cmd][0]==0)) pipe(fd[fdtoindex(me)][next_cmd]);

		// printf("%d\n",next_count);
		// printf("%s:\n",args[0]);
		// printf("now:%d,fd[%d][0]:%d,fd[%d][1]:%d\n",cmd_no,cmd_no,fd[cmd_no][0],cmd_no,fd[cmd_no][1]);
		// printf("----next:%d,fd[%d][0]:%d,fd[%d][1]:%d----\n",next_cmd,next_cmd,fd[next_cmd][0],next_cmd,fd[next_cmd][1]);
		
		FILE *fp;
		int tofiles=tofile(args);
		int fd_com[2];
		int success_to=0;
		int err=0;

		int from_other=0;
		int from_who=fromwho();
		// printf("%d\n",from_who-1);
		// printf("[%d][%d]%d\n",from_who-1,fdtoindex(me),client_pipe[from_who-1][fdtoindex(me)]);
		if(from_who>-1){
			// printf("[%d][%d]%d\n",from_who-1,fdtoindex(me),client_pipe[from_who-1][fdtoindex(me)]);
			if(clients[from_who-1].fd==0 || client_pipe[from_who-1][fdtoindex(me)]==0){
				// printf("[from]the client doesn't exist\n");
				printf("*** Error: the pipe #%d->#%d does not exist yet. ***\n",from_who,fdtoindex(me)+1);
				err=1;
				_break=1;
			}else {
				from_other=1;
				sprintf(mes,"*** %s (#%d) just received from %s (#%d) by '%s' ***\n",clients[fdtoindex(me)].name,
					fdtoindex(me)+1,clients[from_who-1].name,from_who,thecmd);
				broadcast(mes,me);
				// printf("from%s\n",clients[from_who-1].name);
			}
		}

		if(next_count==0){
			if(tofiles){
				fp=fopen(filename,"w");
				dup2(fileno(fp),1);
			}
			else if (to_other){
				// printf("from %d to %d \n",fdtoindex(me),towho-1);
				if(clients[towho-1].fd==0){
					// printf("the client doesn't exist\n");
					printf("*** Error: user #%d does not exist yet. ***\n",towho);
					err=1;
				}else if (client_pipe[fdtoindex(me)][towho-1]!=0){
					// printf("already exist\n");
					printf("*** Error: the pipe #%d->#%d already exists. ***\n",fdtoindex(me)+1,towho);
					err=1;
				}else {
					// printf("-----to %s\n",clients[towho-1].name);
					success_to=1;
					pipe(fd_com);
					client_pipe[fdtoindex(me)][towho-1]=fd_com[0];
					sprintf(mes,"*** %s (#%d) just piped '%s' to %s (#%d) ***\n",clients[fdtoindex(me)].name,
						fdtoindex(me)+1,thecmd,clients[towho-1].name,towho);
					broadcast(mes,me);
					// printf("[%d][%d]%d\n",fdtoindex(me),towho-1,client_pipe[fdtoindex(me)][towho-1]);
					dup2(fd_com[1],1);
					
				}
				// printf("--%d\n",towho);
				// printf("%d,%d\n",fd_com[0],fd_com[1]);
			}
		}
		

		//execute the command!
		if(!err){
			int status;
			int pid=fork();
			if(pid==0){  //child process, execute the command
				if(next_count!=0) {      //write to pipe.
					// printf("write to %d\n",fd[next_cmd][1]);
					dup2(fd[fdtoindex(me)][next_cmd][1],1);
				}
				if(fd[fdtoindex(me)][cmd_no[fdtoindex(me)]][0]!=0 && !self) {    //get the pipe input.
					close(fd[fdtoindex(me)][cmd_no[fdtoindex(me)]][1]);
					dup2(fd[fdtoindex(me)][cmd_no[fdtoindex(me)]][0],0);
				}

				//test,from other.
				// printf("%d\n",client_pipe[1][0]);
				if (from_other){
					// printf("OK\n");
					dup2(client_pipe[from_who-1][fdtoindex(me)],0);
				}

				if (execvp(args[0],args) == -1){
					dup2(2,1);
					printf("Unknown command: [%s].\n", args[0]);
					_exit(EXIT_FAILURE); // If child fails
				}

			}else{   //parent process
				//test, from other.
				if (from_other){
					// printf("close OK\n");
					close(client_pipe[from_who-1][fdtoindex(me)]);
					client_pipe[from_who-1][fdtoindex(me)]=0;
				}

				close(fd[fdtoindex(me)][cmd_no[fdtoindex(me)]][1]);
				waitpid(pid,&status,0);
				if(next_count==0){
					if(tofiles){
						close(fileno(fp));
					}
					else if (to_other && success_to){
						// printf("close:%d\n",fd_com[1]);
						close(fd_com[1]);
					}
				}
				// printf("status:%d\n",status);
				if(status==256  && !first) {
					if(next_count>0 && !inuse){
						close(fd[fdtoindex(me)][next_cmd][0]);
						close(fd[fdtoindex(me)][next_cmd][1]);
						fd[fdtoindex(me)][next_cmd][0]=0;
						fd[fdtoindex(me)][next_cmd][1]=0;
					}
					int fakefd[2];
					pipe(fakefd);
					close(fakefd[0]);
					fd[fdtoindex(me)][cmd_no[fdtoindex(me)]][1]=fakefd[1];
					//fake fd

					--cmd_no[fdtoindex(me)];
					_break=1;
				}
				else{
					close(fd[fdtoindex(me)][cmd_no[fdtoindex(me)]][0]);
					fd[fdtoindex(me)][cmd_no[fdtoindex(me)]][0]=0;
					fd[fdtoindex(me)][cmd_no[fdtoindex(me)]][1]=0;
				}
				// wait(NULL);
			}
		}
	}	
}

int pipein_num(int* pipein){
	int	i=0;
	for(int j=0;j<1024;j++){
		if(pipein[j]==0) break;
		++i; 
	}
	return i;
}

int tofile(char *args[]){
	int ret=0;
	for(int i=0;i<512;i++){
		if(args[i]==NULL) break;
		if(strcmp(args[i],">") == 0){
			ret=1;
			filename=args[i+1];
			args[i]=NULL;
			break;
		}
	}
	return ret;
}

static int fromwho(void){
	int ret=-1;
	for(int i=0;i<512;i++){
		if(args[i]==NULL) break;
		int j=0;
		while(args[i][j]!='\0'){
			if(args[i][j]=='<') {
				// printf("%c\n",args[i][j]);
				ret=target(args[i]);
				args[i]=NULL;
				// printf("--%d\n",target(args[i]));
				return ret;
				// printf("%d",target(args[i]));
				break;
			}else j++;
		}
		
	}
	return ret;
}