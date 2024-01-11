#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/select.h>
#include <stdbool.h>
#include <stdlib.h>

typedef	struct sockaddr_in servaddr;

typedef struct 
{
    int     clientId;
    int     socketId;
    char    *writeBuffer;
} t_client;


typedef struct 
{
    int         serverFd;
    int         maxFd;
    int         numberClients;
    int         clientsId;
    fd_set      writeSet;
    fd_set      readSet;
    t_client    *clients;
    struct      sockaddr_in servaddr;
    struct      sockaddr_in clientAddr;
    bool        active;
} t_server;


t_server    *s()
{
    static t_server    server;
    return (&server);
}

void    fatal(char *msg)
{
    perror("Perror: ");
    write(2, &(*msg), strlen(msg));
    close(s()->serverFd);
    exit(1);
}


int extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

void    dispatchMessage(t_client *client, char *msg, bool anounce)
{
    char *message = calloc(1, strlen(msg) + 100);
    if (anounce)
        sprintf(message, "server: client %d %s\n",client->clientId, msg);
    else
        sprintf(message, "client %d %s",client->clientId, msg);
    for (int i = 0; i < s()->numberClients; i++)
    {
        if (s()->clients[i].socketId == client->socketId)
            continue;
        if (send(s()->clients[i].socketId, message, strlen(message), 0) < 0)
        {
            free(message);
            fatal("Fatal Error\n");
        }
    }
    free(message);
}


void    printClientList()
{
    printf("---- Client List ----\n");
    for (int i = 0; i < s()->numberClients; i++)
    {
        printf("Client id: %d Client Socket Id: %d\n", s()->clients[i].clientId, s()->clients[i].socketId);
        printf("Max FD: %d\n", s()->maxFd);
        printf("Number Clients: %d\n", s()->numberClients);
    }
    printf("--------------------\n");
}

void    acceptConnection()
{
    int     newFd;
    socklen_t len = sizeof(s()->clientAddr);

	newFd = accept(s()->serverFd, (struct sockaddr *)&s()->clientAddr, &len);
	if (newFd < 0)
        fatal("Fatal Error\n"); 
    s()->numberClients++;
    s()->clients = realloc(s()->clients, sizeof(t_client) * s()->numberClients);
    s()->clients[s()->numberClients - 1].clientId = s()->clientsId;
    s()->clientsId++;
    s()->clients[s()->numberClients - 1].socketId = newFd;
    FD_SET(newFd, &s()->writeSet);
    s()->clients[s()->numberClients - 1].writeBuffer = NULL;
    printf("CONNECTED CLIENT...\n");
    printClientList();
    dispatchMessage(&s()->clients[s()->numberClients - 1], "just arrived", true);
}

t_client    *getClient(int fd)
{
    for (int i = 0; i < s()->numberClients; i++)
        if (s()->clients[i].socketId == fd)
            return (&s()->clients[i]);
    return (NULL);
}

void    removeClient(int fd)
{
    int         index = 0;
    for (; index < s()->numberClients; index++)
    {
        if (s()->clients[index].socketId == fd)
            break ;
    }
    printf("fd: %d\n ", fd);
    printf("Index: %d\n ", index);
    dispatchMessage(&s()->clients[index], "just left", true);
    s()->numberClients--;
    close(fd);
    if (s()->clients[index].writeBuffer)
        free(s()->clients[index].writeBuffer);
    s()->clients[index] = s()->clients[s()->numberClients];
    s()->clients = realloc(s()->clients, sizeof(t_client) * s()->numberClients);
    printf("REMOVED CLIENT...\n");
    printClientList();
}

void    handleMessage(int fd, char *msg)
{
    t_client *client = getClient(fd);
    
    client->writeBuffer = str_join(client->writeBuffer, msg);
    if (!client->writeBuffer)
    {
        free(msg);
        fatal("Fatal Error\n");
    }
    int ret;
    while ((ret = extract_message(&client->writeBuffer, &msg)) == 1)
    {
        dispatchMessage(client, msg, false);
        free(msg);
    }
    if (ret == -1)
        fatal("Fatal Error\n");
}

void    socketActivity()
{
    char    *buffer = calloc(1, 4097);
    int     readbytes;
    for (int fd = s()->serverFd; fd <= s()->maxFd; fd++)
    {
        bzero(buffer, 4097);
        if (FD_ISSET(fd, &s()->readSet))
        {
            if (fd == s()->serverFd)
                acceptConnection();
            else
            {
                if ((readbytes = recv(fd, buffer, sizeof(buffer - 1), 0)) <= 0)
                    removeClient(fd);
                else
                {
                    handleMessage(fd, buffer);
                }
            }
        }
    }
}



void    initServer(int port)
{
    s()->serverFd = socket(AF_INET, SOCK_STREAM, 0); 
	if (s()->serverFd == -1) 
		fatal("Fatal Error\n");
	bzero(&s()->servaddr, sizeof(s()->servaddr)); 

	// assign IP, PORT 
	s()->servaddr.sin_family = AF_INET; 
	s()->servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	s()->servaddr.sin_port = htons(port);
  
	// Binding newly created socket to given IP and verification 
	if ((bind(s()->serverFd, (const struct sockaddr *)&s()->servaddr, sizeof(s()->servaddr))) != 0)
		fatal("Fatal Error\n");
	else
		printf("Socket successfully binded..\n");
	if (listen(s()->serverFd, 10) != 0)
		fatal("Fatal Error\n");
    s()->active = true;
    s()->clientsId = 0;
    s()->numberClients = 0;
}

void    initSet(fd_set *set)
{
    FD_ZERO(set);
    FD_SET(s()->serverFd, set);
    s()->maxFd = s()->serverFd;
    for (int i = 0; i < s()->numberClients; i++)
    {
        FD_SET(s()->clients[i].socketId, set);
        if (s()->maxFd < s()->clients[i].socketId)
            s()->maxFd = s()->clients[i].socketId;
    }
}

int main(int ac, char **av)
{
    if (ac != 2)
    {
        write (2, "Wrong Number of arguments\n", strlen("Wrong Number of arguments\n"));
        exit(1);
    }
    initServer(atoi(av[1]));
    while (s()->active)
    {
        initSet(&s()->writeSet);
        initSet(&s()->readSet);
        if (select(s()->maxFd + 1, &s()->readSet, &s()->writeSet, NULL, NULL) < 0)
            fatal("Fatal Error\n");
        socketActivity();
    }
}
