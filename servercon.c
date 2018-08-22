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
#include <sys/shm.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PORT "6666"  // the port users will be connecting to

#define BACKLOG 5 // how many pending connections queue will hold

static char* next;
static char* next_arg;
static char* args[512];
static char* digi;
static int next_count;
static int cmd_no=0;
static char* ptr_env;
static int _break=0;
static int first;
static char* filename;
static int to_other=0;
static char* other;
static int towho;

// functions.
int newid(void);
void addtotable(int id,int pid);
void delfromtable(int id);
void clearall(void);
int fromwho(void);
static char thecmd[10000];


void broadcast(int signum)
{
	if (signum == SIGUSR1)
	{
		key_t key3;
		int shm_id3;
		char* message;
		key3=333;
		shm_id3 = shmget(key3,10000, 0666 | IPC_CREAT);
		message =shmat(shm_id3, NULL, 0);	
		
		// printf("broad pointer:%p\n",message );
		printf("%s\n",message);
		shmdt(message);
	}
	if (signum == SIGUSR2)
	{
		key_t key4;
		int shm_id4;
		int *client_pipe;
		key4=44;
		shm_id4 = shmget(key4,sizeof(int)*(30*30+2), 0666 | IPC_CREAT);
		client_pipe =shmat(shm_id4, NULL, 0);

		int from=client_pipe[900];
		int to=client_pipe[901];
		// client_pipe[900],client_pipe[901]
		int fdfifo;
		char * myfifo = "./.myfifo";
		// char myfifos[50];
		// char * myfifo=myfifos;
		// sprintf(myfifo,"/home/ekko/fifodata/myfifo-%d%d",from,to);
		mkfifo(myfifo, 0666);
		fdfifo = open(myfifo, O_RDONLY | O_NONBLOCK);   //| O_NONBLOCK
		client_pipe[from*30+(to-1)]=fdfifo;
		// printf("index:(%d,%d),readfd:%d\n",from,to-1,client_pipe[from*30+(to-1)]);
		shmdt(client_pipe);

	}
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

static void split(char* cmd);
static int ispipe(char* cmd);
static void run(int type,int me);
static int fd[1024][2];
static int tofile(char *args[]);


int target(char* cmd);

typedef struct{
	int pid;
	char name[20];
	char ip[15];
	int port;
	// char path[50]; //their own environment.
}clientdata;

static int pipein_num(int* pipein);


void cmdnonr(char* cmd);


int main(void)
{
	clearall();  //clear all user.
	setenv("PATH","bin:.",1);
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}
	printf("server: waiting for connections...\n");
	signal(SIGUSR1,broadcast);
	signal(SIGUSR2,broadcast);
	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}


		inet_ntop(their_addr.ss_family,get_in_addr((struct sockaddr *)&their_addr),s, sizeof s);
		printf("selectserver: new connection from %s on socket %d\n",
						s,new_fd);

		// printf("%p,%p\n",(void *)0,NULL );
		int pid=fork();

		if (pid==0) { // this is the child process
			// printf("%d\n",getpid() );
			dup2(new_fd,1);
			dup2(new_fd,2);
			close(sockfd); // child doesn't need the listener
			int me =newid();
			char client_message[10000],*welcome="****************************************\n"
			"** Welcome to the information server. **\n"
			"****************************************\n",*sym="% ";
			char *buffer;
			send(new_fd, welcome,strlen(welcome),0);
			addtotable(me,getpid());

			while(1){
				dup2(new_fd,1);
				dup2(new_fd,2);

				first=1;
				bzero(client_message,10000);
				send(new_fd, sym,strlen(sym),0);

				int count=0;
				int flag=1;

				while(flag){
					char tmp[20]={0};
					count+=read(new_fd,tmp,20);
					// printf("%d\n",count);
					strcat(client_message,tmp);
					// printf("%s\n",client_message);
					if(strstr(tmp,"\r\n")!= NULL || strstr(tmp,"\n")!= NULL ) {
						flag=0;	
					}
					// printf("%d\n",flag);
				}

				if(count<=0) {
					delfromtable(me);
					exit(0);
				}

				// char thecmd[10000];  //need to share this memory
				bzero(thecmd,10000);
				strcpy(thecmd,client_message);
				cmdnonr(thecmd);
				to_other=0;
				// int saveout=dup(1);
				// int saveerr=dup(2);
				// dup2(new_fd,1);
				// dup2(new_fd,2);

				char* cmd=client_message;
				char *otehr;
				other=strchr(cmd,'>');
				if (other!=NULL) {
					other++;
					if(isdigit(*other)){
						to_other=1;
					}else{
						;
					}
					other--;
					if(to_other) {
						*other=' ';
						towho=target(other);
					}
				}
				next=strchr(cmd,'|');
				if(next!=NULL) *next='\0';
				while(next!=NULL){
					first=0;
					_break=0;
					split(cmd);
					run(ispipe(next),me);
					++cmd_no;
					if(_break) break;   //unknow command
					cmd=next;
					next=strchr(cmd,'|');
					if(next==NULL) break;
					*next='\0';
				}
				if(!_break){
					split(cmd);
					run(0,me);
					++cmd_no;
				}
				_break=0;
				// dup2(saveout,1);
				// dup2(saveerr,2);
				// close(saveout);
				// close(saveerr);
			}
		}
		close(new_fd);// parent doesn't need this
	}

	return 0;
}
static char* skipwhite(char* s)
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


static int ispipe(char* cmd){
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

static void run(int next_count,int me){
	// printf("the cmd:%s\n",thecmd );
	//attach shared memory
	key_t key,key2;
	int shm_id,shm_id2;
	clientdata* clients;
	/* get the ID of shared memory */
	key=1;
	shm_id = shmget(key,sizeof(clientdata)*30 , 0666 | IPC_CREAT);
	clients =shmat(shm_id, NULL, 0);
	int* total_cli;
	key2=2;
	shm_id2 = shmget(key2,sizeof(int), 0666 | IPC_CREAT);
	total_cli =shmat(shm_id2, NULL, 0);
	
	key_t key3;
	int shm_id3;
	char* message;
	key3=333;
	shm_id3 = shmget(key3,10000, 0666 | IPC_CREAT);
	message =shmat(shm_id3, NULL, 0);


	key_t key4;
	int shm_id4;
	int *client_pipe;
	
	key4=44;
	shm_id4 = shmget(key4,sizeof(int)*(30*30+2), 0666 | IPC_CREAT);
	client_pipe =shmat(shm_id4, NULL, 0);

	// printf("%d\n",cmd_no);
	int inuse=0;
	if(args[0] == NULL){
		--cmd_no;
	}
	else if (strcmp(args[0], "exit") == 0) {
		delfromtable(me);
		exit(0);
	}
	else if(strcmp(args[0], "setenv") == 0){
		if(args[1]!=NULL&&args[2]!=NULL) setenv(args[1],args[2],1);
	}else if(strcmp(args[0], "printenv") == 0){
		if(args[1]!=NULL){
			char *ptr_path = getenv(args[1]);
			printf("%s=%s\n",args[1],ptr_path);
		}
	}else if(strcmp(args[0], "yell") == 0) {
		sprintf(message,"*** %s yelled ***: %s",clients[me].name,args[1]);
		// strcpy(message,args[1]);
		int cut=0;
		int i=0;
		while(cut!=*total_cli){
			if(clients[i].pid!=0){
				kill(clients[i].pid,SIGUSR1);
				cut++;
			}
			i++;
		}
	}
	else if(strcmp(args[0], "tell") == 0){
		//broadcast!
		if (clients[atoi(args[1])-1].pid==0){
			printf("*** Error: user #%d does not exist yet. ***\n",atoi(args[1]));
		}else{
			sprintf(message,"*** %s told you ***: %s",clients[me].name,args[2]);
			kill(clients[atoi(args[1])-1].pid,SIGUSR1);
		}
	}else if(strcmp(args[0], "who") == 0){
		int total=0;
		int i=0;
		printf("<ID>\t<nickname>\t<IP/port>\t<indicate me>\n");
		while(total!=*total_cli){
			if(clients[i].pid!=0){
				total++;
				if(i==me){
					printf("%d\t%s\t%s/%d\t<-me\n",(i+1),clients[i].name,clients[i].ip,clients[i].port);	
				}
				else
				{
					printf("%d\t%s\t%s/%d\n",(i+1),clients[i].name,clients[i].ip,clients[i].port);
				}
			}
			i++;
		}
	}else if(strcmp(args[0], "name") == 0){
		int total=0;
		int i=0;
		int used=0;
		while(total!=*total_cli){
			if(clients[i].pid!=0){
				total++;
				if(strcmp(args[1], clients[i].name) == 0){
					used=1;
					printf("*** User '%s' already exists. ***\n",args[1]);
					break;
				}
			}
			i++;
		}
		if (!used){
			strcpy(clients[me].name,args[1]);
			sprintf(message,"*** User from %s/%d is named '%s'. ***",clients[me].ip,clients[me].port,clients[me].name);
			int cut=0;
			int i=0;
			while(cut!=*total_cli){
				if(clients[i].pid!=0){
					kill(clients[i].pid,SIGUSR1);
					cut++;
				}
				i++;
			}
		}
	}
	else{
		int self=0;  // 0 is not self, 1 is self.
		if(cmd_no>1023) cmd_no-=1024;
		if(fd[cmd_no][0]==0) { // check pipe open or not.
			pipe(fd[cmd_no]);
			self=0;
		}
		int next_cmd=cmd_no+next_count;
		if(next_cmd>1023) next_cmd-=1024;
		if(fd[next_cmd][0]!=0) inuse=1;
		if((next_count!=0) && (fd[next_cmd][0]==0)) pipe(fd[next_cmd]);
		
		FILE *fp;
		int tofiles=tofile(args);

		int success_to=0;
		int err=0;
		int fdfifo;

		int from_other=0;
		int from_who=fromwho();
		if(from_who>-1){  // read from fifo messaage
			if(clients[from_who-1].pid==0 || client_pipe[(from_who-1)*30+me]==0){
				// printf("[from]the client doesn't exist\n");
				printf("*** Error: the pipe #%d->#%d does not exist yet. ***\n",from_who,me+1);
				err=1;
				_break=1;
			}else {
				from_other=1;
				sprintf(message,"*** %s (#%d) just received from %s (#%d) by '%s' ***",clients[me].name,
					me+1,clients[from_who-1].name,from_who,thecmd);
				int cut=0;
				int i=0;
				while(cut!=*total_cli){
					if(clients[i].pid!=0){
						kill(clients[i].pid,SIGUSR1);
						cut++;
					}
					i++;
				}usleep(200);
				// printf("from%s\n",clients[from_who-1].name);
			}
		}

		if(next_count==0){
			if(tofiles){
				fp=fopen(filename,"w");
				dup2(fileno(fp),1);
			}
			else if (to_other){  //write to fifo
				// printf("from %d to %d \n",me+1,towho);
				if(clients[towho-1].pid==0){
					// printf("the client doesn't exist\n");
					printf("*** Error: user #%d does not exist yet. ***\n",towho);
					err=1;
				}else if (client_pipe[me*30+(towho-1)]!=0){
					// printf("already exist\n");
					printf("*** Error: the pipe #%d->#%d already exists. ***\n",me+1,towho);
					err=1;
				}else {
					// printf("-----to %s\n",clients[towho-1].name);
					success_to=1;
					// pipe(fd_com);
					// client_pipe[me*30+(towho-1)]=1;
					sprintf(message,"*** %s (#%d) just piped '%s' to %s (#%d) ***",clients[me].name,
						me+1,thecmd,clients[towho-1].name,towho);
					int cut=0;
					int i=0;
					while(cut!=*total_cli){
						if(clients[i].pid!=0){
							kill(clients[i].pid,SIGUSR1);
							cut++;
						}
						i++;
					}
					client_pipe[900]=me;
					client_pipe[901]=towho;
					kill(clients[towho-1].pid,SIGUSR2);
					char * myfifo="./.myfifo";
					// char myfifos[50];
					// char * myfifo=myfifos;
					// sprintf(myfifo,"/home/ekko/fifodata/myfifo-%d%d",me,towho);
					mkfifo(myfifo, 0666);
					fdfifo = open(myfifo, O_WRONLY);
					// printf("%d\n",fdfifo);
					dup2(fdfifo,1);
					unlink(myfifo);
					// printf("[%d][%d]%d\n",fdtoindex(me),towho-1,client_pipe[fdtoindex(me)][towho-1]);
					// dup2(fd_com[1],1);
					
				}
				// // printf("--%d\n",towho);
				// // printf("%d,%d\n",fd_com[0],fd_com[1]);
			}
		}
		if(!err){
			int status;
			int pid=fork();
			if(pid==0){  //child process
				if(next_count!=0) {      //write to pipe.
					dup2(fd[next_cmd][1],1);
				}
				if(fd[cmd_no][0]!=0 && !self) {    //get the pipe input.
					close(fd[cmd_no][1]);
					dup2(fd[cmd_no][0],0);
				}
				if (from_other){  // read the fifo
					// printf("OK\n");
					// printf("from other\n");
					// printf("---intdex:(%d,%d),readfd:%d\n",from_who-1,me,client_pipe[(from_who-1)*30+me]);
					dup2(client_pipe[(from_who-1)*30+me],0);
					client_pipe[(from_who-1)*30+me]=0;
					// dup2(client_pipe[from_who-1][fdtoindex(me)],0);
				}

				if (execvp(args[0],args) == -1){
					dup2(2,1);
					printf("Unknown command: [%s].\n", args[0]);
					_exit(EXIT_FAILURE); // If child fails
				}

			}else{   //parent process
				if (from_other){   // close the pipe
					// printf("close OK\n");
					close(client_pipe[(from_who-1)*30+me]);
					
					// close(client_pipe[from_who-1][fdtoindex(me)]);
					// client_pipe[from_who-1][fdtoindex(me)]=0;
				}

				close(fd[cmd_no][1]);
				waitpid(pid,&status,0);
				if(next_count==0){
					if(tofiles){
						close(fileno(fp));
					}
					else if (to_other && success_to){  //close the write fifo
						// printf("close:%d\n",fd_com[1]);
						// close(fd_com[1]);
						close(fdfifo);
					}
				}
				// printf("status:%d\n",status);
				if(status==256  && !first) {
					if(next_count>0 && !inuse){
						close(fd[next_cmd][0]);
						close(fd[next_cmd][1]);
						fd[next_cmd][0]=0;
						fd[next_cmd][1]=0;
					}
					int fakefd[2];
					pipe(fakefd);
					close(fakefd[0]);
					fd[cmd_no][1]=fakefd[1];
					//fake fd
					--cmd_no;
					_break=1;
				}
				else{
					close(fd[cmd_no][0]);
					fd[cmd_no][0]=0;
					fd[cmd_no][1]=0;
				}
				// wait(NULL);
			}
		}
	}


	//detatch shared memory
	shmdt(clients);
	shmdt(total_cli);
	shmdt(message);
	shmdt(client_pipe);
}

static int pipein_num(int* pipein){
	int	i=0;
	for(int j=0;j<1024;j++){
		if(pipein[j]==0) break;
		++i; 
	}
	return i;
}

static int tofile(char *args[]){
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
int newid(void){
	int ret=-1;
	key_t key;
	int shm_id;
	clientdata* clients;
	/* get the ID of shared memory */
	key=1;
	shm_id = shmget(key,sizeof(clientdata)*30 , 0666 | IPC_CREAT);
	clients =shmat(shm_id, NULL, 0);
	for(int i=0;i<30;i++){
		if(clients[i].port==0) {
			ret=i;
			break;
		}
	}
	shmdt(clients);
	return ret;
}


void addtotable(int id,int pid){
	key_t key,key2;
	int shm_id,shm_id2;
	clientdata* clients;
	/* get the ID of shared memory */
	key=1;
	key2=2;
	shm_id = shmget(key,sizeof(clientdata)*30 , 0666 | IPC_CREAT);
	clients =shmat(shm_id, NULL, 0);

	int* total_cli;
	key2=2;
	shm_id2 = shmget(key2,sizeof(int), 0666 | IPC_CREAT);
	total_cli =shmat(shm_id2, NULL, 0);

	//initial
	clients[id].pid=pid;
	strcpy(clients[id].name,"(no name)");
	strcpy(clients[id].ip,"CGILAB");
	clients[id].port=511;

	*total_cli=*total_cli+1;

	key_t key3;
	int shm_id3;
	char* message;
	key3=333;
	shm_id3 = shmget(key3,10000, 0666 | IPC_CREAT);
	message =shmat(shm_id3, NULL, 0);

	//broadcast login	
	int cut=0;
	int i=0;
	sprintf(message,"*** User '%s' entered from %s/%d. ***",clients[id].name,clients[id].ip,clients[id].port);	
	while(cut!=*total_cli){
		if(clients[i].pid!=0){
			kill(clients[i].pid,SIGUSR1);
			cut++;
		}
		i++;
	}
	shmdt(clients);
	shmdt(total_cli);
	shmdt(message);
}

void delfromtable(int id){
	key_t key,key2;
	int shm_id,shm_id2;
	clientdata* clients;
	/* get the ID of shared memory */
	key=1;
	shm_id = shmget(key,sizeof(clientdata)*30 , 0666 | IPC_CREAT);
	clients =shmat(shm_id, NULL, 0);

	int* total_cli;
	key2=2;
	shm_id2 = shmget(key2,sizeof(int), 0666 | IPC_CREAT);
	total_cli =shmat(shm_id2, NULL, 0);

	key_t key3;
	int shm_id3;
	char* message;
	key3=333;
	shm_id3 = shmget(key3,10000, 0666 | IPC_CREAT);
	message =shmat(shm_id3, NULL, 0);

	//broadcast logout	
	int cut=0;
	int i=0;
	sprintf(message,"*** User '%s' left. ***",clients[id].name);
	while(cut!=*total_cli){
		if(clients[i].pid!=0){
			kill(clients[i].pid,SIGUSR1);
			cut++;
		}
		i++;
	}

	key_t key4;
	int shm_id4;
	int *client_pipe;
	key4=44;
	shm_id4 = shmget(key4,sizeof(int)*(30*30+2), 0666 | IPC_CREAT);
	client_pipe =shmat(shm_id4, NULL, 0);

	for (int i = 0; i < 30; ++i)
	{
		client_pipe[i*30+id]=0;
	}

	//initial
	clients[id].pid=0;
	bzero(clients[id].name,20);
	bzero(clients[id].ip,15);
	clients[id].port=0;

	*total_cli=*total_cli-1;	

	
	shmdt(clients);
	shmdt(total_cli);
	shmdt(message);
	shmdt(client_pipe);

}

void clearall(void){
	key_t key,key2;
	int shm_id,shm_id2;
	clientdata* clients;
	/* get the ID of shared memory */
	key=1;
	shm_id = shmget(key,sizeof(clientdata)*30 , 0666 | IPC_CREAT);
	clients =shmat(shm_id, NULL, 0);

	int* total_cli;
	key2=2;
	shm_id2 = shmget(key2,sizeof(int), 0666 | IPC_CREAT);
	total_cli =shmat(shm_id2, NULL, 0);

	key_t key4;
	int shm_id4;
	int *client_pipe;
	key4=44;
	shm_id4 = shmget(key4,sizeof(int)*(30*30+2), 0666 | IPC_CREAT);
	client_pipe =shmat(shm_id4, NULL, 0);

	for (int i = 0; i < 30; ++i)
	{
		for (int j = 0; j < 30; ++j)
		{
			client_pipe[i*30+j]=0;
		}
	}

	//initial
	for(int i=0;i<30;i++){
		clients[i].pid=0;
		bzero(clients[i].name,20);
		bzero(clients[i].ip,15);
		clients[i].port=0;
	}
	
	*total_cli=0;	

	
	shmdt(clients);
	shmdt(total_cli);
	shmdt(client_pipe);

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


int fromwho(void){
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