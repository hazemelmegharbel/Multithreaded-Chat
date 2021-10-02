#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<netdb.h>
#include <pthread.h>
#include<stdbool.h>
#include "list.h"
struct UDPComms //this is the struct that i created to hold the command line arguments(user port and the machine name) that will be passed in main
{
    char port[20];
    char MachineName[20];
};

typedef struct UDPComms Comms;
pthread_cond_t emptykeyandsend;
pthread_cond_t emptyscreenandrecv; //condition vars to wait when the list is empty
pthread_mutex_t mutexforsendandkeyboard; //mutexes for list access
pthread_mutex_t mutexforrecvandscreen;
LIST* keyandsendlist;
LIST* recvandscreenlist; //my lists one for send and the keyboard threads and the other for receive and screen


int testForExclam=0;
void* printToScreen(void* args)
{
    char* messageptr=(char*)malloc(256);    //a char* to hold casted info returned by ListTrim
    char message[256]; //the actual string
    bool printThreadQuit=false;     //flag to know when to quit
    LIST* recvandscreenlist=(LIST*)args;    //the LIST* was passed as an argument of type void* so i casted it in here
    while(1)
    {
        pthread_mutex_lock(&mutexforrecvandscreen);     //"I want to access critical section"
        while(recvandscreenlist->mynodes==0)
        {
            pthread_cond_wait(&emptyscreenandrecv,&mutexforrecvandscreen); //will wait on the condition that the list is empty
        }
        messageptr=(char*)ListTrim(recvandscreenlist); //FIFO structure, so since i prepend later the logical decision is to trim
        int i=0;
        while(messageptr[i]!='\0')
        {
            message[i]=messageptr[i];
            i++;
        }
        message[i]='\0';    //copys over the data from the char* to the regular string
        if(sizeof(message)>0)
        {
            printf("\nrecieved message from sender: %s \n", message); //prints
        }
        pthread_mutex_unlock(&mutexforrecvandscreen); //    "Im done"
        int loop=0;
        while(message[loop]!='\0')
        {
            if(message[loop]=='!')  /*this code is just to check for the ! char and exits*/
            {
                printThreadQuit=true;
                break;
            }
            loop++;
        }
        if(printThreadQuit==true)
        {
            break;
        }
    }
    pthread_exit(NULL);
}

void* receiveMessage(void* recvoid)
{
    Comms* recvptr=(Comms*)recvoid;
    Comms rec=*recvptr;
    struct sockaddr_in myaddr, remoteaddr;
    socklen_t addrlen=sizeof(remoteaddr);
    int recvsock;
    int bytesrecieved;
    unsigned char buffer[256]; //for receiving
    bool recvquit=false;
    if((recvsock=socket(AF_INET,SOCK_DGRAM,0))<0)
    {
        perror("cannot create the socket\n");
        return  ((void*)0);
    }

    memset((char*)&myaddr,0,sizeof(myaddr));
    myaddr.sin_family=AF_INET;
    myaddr.sin_port=htons(atoi(rec.port));
    myaddr.sin_addr.s_addr= htonl(INADDR_ANY);

    if(bind(recvsock, (struct sockaddr*) &myaddr, sizeof(myaddr))<0)
    {
        perror("bind failed");
        return ((void*)0);
    }
    for(;;)
    {
        bytesrecieved=recvfrom(recvsock,buffer,256,0,(struct sockaddr*)&remoteaddr,&addrlen); //wait on the port
        pthread_mutex_lock(&mutexforrecvandscreen); //"I want to access ctitical section"
        ListPrepend(recvandscreenlist,(void*)buffer); //prepends to list what has been recieved on the port passed in from the command line
        if(recvandscreenlist->mynodes >0)
        {
            pthread_cond_signal(&emptyscreenandrecv);   //signals the other process that the buffer is not empty anymore so if youre waiting you can proceed
        }
        pthread_mutex_unlock(&mutexforrecvandscreen);
        int k=0;
        while(buffer[k]!='\0')
        {
            if(buffer[k]=='!')
            {
                recvquit=true;  //code to exit thread
                break;
            }
            k++;
        }
        if(recvquit==true)
        {
            break;
        }
    }
    printf("receive thread quitting\n");
    close(recvsock);
    pthread_exit(NULL);
}
void* keyboard(void* listptr)
{
    LIST* keyandsendlist= (LIST*)listptr;  //the LIST* was passed as an argument of type void* so i casted it in here
    char message[256]; //the actual string
    bool keyExit=false;
    while(1)
    {
        printf("Message to send over: "); //prompt to enter the message
        fgets(message, 256, stdin); //grab message from command line to store
        int i=0;
        while(message[i]!='\0')
        {
            if(message[i]=='\n')
            {
                message[i]='\0'; //fgets will actually store the \n char so i had to manually replace it with the null char for a better look 
                break;
            }
            i++;
        }
        pthread_mutex_lock(&mutexforsendandkeyboard); //block incoming processes
        ListPrepend(keyandsendlist, (void*)message); //prepends the data to the critical section; the shared list
        if(keyandsendlist->mynodes >0)
        {
            pthread_cond_signal(&emptykeyandsend); //signal the send thread that it can trim with no issues if it was waiting on the empty list
        }
        pthread_mutex_unlock(&mutexforsendandkeyboard); //allows other processes into critical section
        int loop=0;
        while(message[loop]!='\0')
        {
            if(message[loop]=='!')
            {
                keyExit=true;
                break;
            }
            loop++;
        }
        if(keyExit==true)
        {
            break;
        }
    }
    pthread_exit(NULL);
}
void* sendMessage(void* sndvoid)
{
    Comms* sndptr=(Comms*)sndvoid;
    Comms snd=*sndptr;
    int sendSocket;
    struct addrinfo hints, *serverinfo, *pointer;
    int somev;
    int numbytes;
    char message[256];
    char* messageptr=(char*)malloc(256);
    bool sendquit=false;
    memset(&hints, 0, sizeof hints);
    hints.ai_family= AF_INET;
    hints.ai_socktype= SOCK_DGRAM;
    if((somev=getaddrinfo(snd.MachineName,snd.port,&hints,&serverinfo))!=0)
    {
        fprintf(stderr,"getaddrinfo: %s\n", gai_strerror(somev));
        return ((void*)1);
    }
    for(pointer=serverinfo; pointer!=NULL; pointer=pointer->ai_next)
    {
        if((sendSocket=socket(pointer->ai_family,pointer->ai_socktype,pointer->ai_protocol))==-1)
        {
            perror("talker: socket");
            continue;
        }
        break;
    }
    if(pointer==NULL)
    {
        fprintf(stderr, "talker: failed to create socket\n");
        return ((void*)2);
    }
    for(;;)
    {
        pthread_mutex_lock(&mutexforsendandkeyboard); //blocks incoming processes 
        while(keyandsendlist->mynodes==0)
        {
            pthread_cond_wait(&emptykeyandsend,&mutexforsendandkeyboard); //will wait on an empty buffer because there will be nothing to trim
        }
        messageptr= (char*)ListTrim(keyandsendlist); //re-casts the data to a char* which i copy each char from into a regular char[] below
        int i=0;
        while(messageptr[i]!='\0')
        {
            message[i]=messageptr[i];
            i++;
        }
        message[i]='\0';
        
        if((numbytes = sendto(sendSocket,&message, 256, 0, pointer->ai_addr, pointer->ai_addrlen))==-1)//sends to user
        {
            perror("talker: sendto");
            exit(1);
        }
        pthread_mutex_unlock(&mutexforsendandkeyboard); //allows other processes in
        int j=0; 
        while(message[j]!='\0')
        {
            if(message[j]=='!')
            {
                sendquit=true;
                break;
            }
            j++;
        }
        if(sendquit==true)
        {
            break;
        }
    }
    printf("You have entered the ! character, so you have exited the program and cannot send any more messages.");
    testForExclam+=3;
    freeaddrinfo(serverinfo);
    close (sendSocket);
    sleep(3);
    pthread_exit(NULL);
}
int main(int argc, char* argv[])
{
    if(argc!=4)
    {
        printf("Must specify 3 arguments: sender port, remote machine name, remote machine port");
        return 0;
    }
    testForExclam=0;
    Comms testSend;     
    Comms testReceive; //create my structs that are ready to recieve the command line arguments
    Comms* testSendptr=&testSend;
    Comms* testReceiveptr=&testReceive;// these i created just so i could cast them into void* so they could be passed to the send and recieve threads
    strcpy(testSend.port, argv[3]);
    strcpy(testSend.MachineName, argv[2]); //enter them accordingly; 1 is my port,2 is remote machine name 3 is remote port
    strcpy(testReceive.port, argv[1]);
    keyandsendlist=ListCreate();
    recvandscreenlist=ListCreate();// call listcreate to initialise lists

    pthread_t sendThread;
    pthread_t recieveThread; //create our 4 threads
    pthread_t keybordThread;
    pthread_t screenPrintThread;
    void* testSendVoid=(void*) testSendptr;
    void* testReceiveVoid=(void*)testReceiveptr;
    pthread_mutex_init(&mutexforsendandkeyboard,NULL);
    pthread_mutex_init(&mutexforrecvandscreen,NULL);
    pthread_cond_init(&emptykeyandsend,NULL); //initialise mutexes
    pthread_cond_init(&emptyscreenandrecv,NULL);
    int ret1=pthread_create(&sendThread,NULL,sendMessage,testSendVoid);
    int ret2=pthread_create(&recieveThread,NULL,receiveMessage,testReceiveVoid); //create the threads
    int ret3=pthread_create(&keybordThread,NULL,keyboard,(void*)keyandsendlist);
    int ret4=pthread_create(&screenPrintThread,NULL,printToScreen,(void*)recvandscreenlist);

    int retJoin1=pthread_join(&sendThread, NULL);
    int retJoin2=pthread_join(&recieveThread,NULL);
    int retJoin3=pthread_join(&keybordThread,NULL); //rejoin them
    int retJoin4=pthread_join(&screenPrintThread,NULL);
    pthread_mutex_destroy(&mutexforrecvandscreen);
    pthread_mutex_destroy(&mutexforsendandkeyboard);
    pthread_cond_destroy(&emptyscreenandrecv); //destroy any condition variables and mutexes i created
    pthread_cond_destroy(&emptykeyandsend);
    return 0;
}
