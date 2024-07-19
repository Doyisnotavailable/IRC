#include "../includes/Server.hpp"
#include "../includes/Util.hpp"
// #include "../includes/Errormsg.hpp"
#include <algorithm>
#include <poll.h>
#include <cstring>

bool stopflag = false;
Server::Server(std::string port, std::string pass) {
    if (port.empty() || pass.empty()) {
		throw emptyArg();
	}
    for (size_t i = 0; i < port.length(); i++) {
        if (!std::isdigit(port[i]))
            throw InvalidInput();
    }

	int a = parse_port(port.c_str());
	if (a == -1)
		throw InvalidPort();

	if (parse_password(pass.c_str()) == -1)
		throw InvalidPassword();
    this->pass = pass;
    this->port = a;
    this->stopflag = false;
	this->clientCount = 0;

    initserverSock();
    startServer();
}

Server::~Server() {
    // close(serverfd); // close all fds
	for (size_t i = 0; i < pollfds.size(); ++i){
		std::cout << "closing port " << pollfds[i].fd << std::endl;
		close(pollfds[i].fd);
	}
}

std::string Server::getHostname() const {
	return this->hostname;
}

void Server::initserverSock() {
    struct sockaddr_in adr;

    adr.sin_port = htons(this->port);
    adr.sin_family = AF_INET;
    adr.sin_addr.s_addr = INADDR_ANY;

    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd == -1)
        throw SockCreation();
    int a = 1;
    if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &a, sizeof(a)) == -1)
        throw Sockaddroption();
    if (fcntl(serverfd, F_SETFL, O_NONBLOCK) == -1)
        throw Sockfdoption();
    if (bind(serverfd, (struct sockaddr *)&adr, sizeof(adr)) == -1)
        throw Sockbind();
    if (listen(serverfd, SOMAXCONN) == -1)
        throw Socklisten();
    
    struct pollfd paul;
    paul.fd = serverfd;
    paul.events = POLLIN;
    paul.revents = 0;
    pollfds.push_back(paul);
}

void Server::startServer() {
    std::cout << "Starting Server" << std::endl;
	char hostname [256];
	if (gethostname(hostname, sizeof(hostname)) != 0)
        throw std::runtime_error("Error: Hostname resolution failed");
	this->hostname = hostname;
	std::cout << "Server listening on host ip: " << hostname << " port: " << this->port << std::endl;

	std::signal(SIGINT, sigHandler);
	std::signal(SIGQUIT, sigHandler);
    while (::stopflag == false) {
        if (poll(&pollfds[0], pollfds.size(), -1) == -1 && stopflag == true)
            throw::InvalidInput();
		usleep(20);
        for (size_t i = 0; i < pollfds.size(); i++) {
            if (pollfds[i].revents && POLLIN && !stopflag) {
                if (pollfds[i].fd == serverfd)
                    Server::addClient();
                else
					Server::receive(pollfds[i].fd);
            }
        }   
    }
}


void Server::eraseClient(Client* cl) {
	// Erase client from all channels
	for (size_t i = 0; i < channels.size(); ++i){
		if (channels[i].checkclientExist(cl)){
			channels[i].removeClient(cl);
			channels[i].removeclientOper(cl);
		}
		if (channels[i].getclientSize() == 0)
			channels.erase(channels.begin() + i);
	}
}

void Server::receive(int fd) {
	char str[512];
	memset(str, 0, sizeof(str));

	ssize_t size = recv(fd, str, sizeof(str) - 1, 0);

	// Parse for maximum size of message
	if (size > 510) {
		Client *cl = getClient(fd);
		if (!cl)
			return ;
		sendToClient(cl->getfd(), ERR_INPUTTOOLONG + cl->getnName() + " :Input line was too long\r\n");
		return ;
	}
	if (size <= 0) {
		std::cout << "Client " << this->getClient(fd)->getfd() << " disconnected" << std::endl;
		Client *cl = getClient(fd);
		if (cl) {
			eraseClient(cl);
			removeClient(fd);
			close(fd);
		}
	} else {
		partialMessage(str, fd);
		std::map<int, std::string>::iterator it = clbuffer.find(fd);

		if (it != clbuffer.end()){
			if (*(it->second.end() - 1) == '\n'){
				std::string line(it->second);
				clbuffer.erase(it);
				std::vector<std::string> vec = splitCmd(line);
				Client *client = getClient(fd);

				if (vec.empty() || !client)
					return ;
				if ((*vec.begin() == "CAP" || *vec.begin() == "PASS" || *vec.begin() == "NICK" || *vec.begin() == "USER")) // For registration
					clAuthentication(fd, vec);
				else if (client->getisCapNegotiated() == true && client->getisRegistered() == true) {
					if (!checkReceived(line, getClient(fd))) // Proccess received message
						return ;
				}
				else
					sendToClient(fd, ERR_NOTREGISTERED ":You have not registered\r\n");
				if (client->getisCapNegotiated() == true || client->getnName().empty() || client->getuName().empty())
					return;
				else
					sendWelcome(fd, client); // Send welcome message to the client(Neccessary for the client to start the connection.)
			}
		}
	}
}

void Server::partialMessage(char* str, int fd){
	std::map<int, std::string>::iterator it = clbuffer.find(fd);
	 std::string tmp(str);
	if (it != clbuffer.end())
		it->second += tmp;
	else
		clbuffer[fd] = str;
}

// Sends welcome messages and server details to the newly connected client. Crucial for the client to start the connection.
void Server::sendWelcome(int fd, Client* client) {
	std::string welcomeMessage = 
		"**********************************************\n"
		"*                                            *\n"
		"*            Welcome to the IRC_M&Ms         *\n"
		"*                                            *\n"
		"*       Developed by Martin and Michael      *\n"
		"*                                            *\n"
		"**********************************************\n";

	sendToClient(fd, ":" + client->getuName() + RPL_WELCOME + client->getnName() + " :Welcome to the IRC_M2 Network, " + client->getnName() + "!~" + client->getuName() + "@" + getHostname() + "\r\n"); // 001 RPL_WELCOME
    sendToClient(fd, ":" + client->getuName() + RPL_YOURHOST + client->getnName() + " :Your host is IRC_M2, running version APEX.1.0\r\n"); // 002 RPL_YOURHOST
    sendToClient(fd, ":" + client->getuName() + RPL_CREATED + client->getnName() + " :This server was created by Martin and Miguel\r\n"); // 003 RPL_CREATED
    sendToClient(fd, ":" + client->getuName() + RPL_MYINFO + client->getnName() + " IRC_M2 APEX.1.0 io ovimnqpsrtklbf :This server supports multiple modes\r\n"); // 004 RPL_MYINFO
    sendToClient(fd, ":" + client->getuName() + RPL_ISUPPORT + client->getnName() + " CHANTYPES=#& PREFIX=(ov)@+ NETWORK=IRC_M2 :are supported by this server\r\n"); // 005 RPL_ISUPPORT

    sendToClient(fd, ":" + client->getuName() + RPL_LUSERCLIENT + client->getnName() + " :There are 10 users and 3 services on 1 server\r\n"); // 251 RPL_LUSERCLIENT
    sendToClient(fd, ":" + client->getuName() + RPL_LUSEROP + client->getnName() + " 2 :operator(s) online\r\n"); // 252 RPL_LUSEROP
    sendToClient(fd, ":" + client->getuName() + RPL_LUSERUNKNOWN + client->getnName() + " 1 :unknown connection(s)\r\n"); // 253 RPL_LUSERUNKNOWN
    sendToClient(fd, ":" + client->getuName() + RPL_LUSERCHANNELS + client->getnName() + " 5 :channels formed\r\n"); // 254 RPL_LUSERCHANNELS
    sendToClient(fd, ":" + client->getuName() + RPL_LUSERME + client->getnName() + " :I have " + intToString(this->clientCount) + " clients and 1 servers\r\n"); // 255 RPL_LUSERME	

    sendToClient(fd, ":" + client->getuName() + RPL_MOTDSTART + client->getnName() + " :- " + client->getuName() + " Message of the Day -\r\n"); // 375 RPL_MOTDSTART
    sendToClient(fd, ":" + client->getuName() + RPL_MOTD + client->getnName() + " :- Welcome to the best IRC server!\r\n"); // 372 RPL_MOTD
    sendToClient(fd, ":" + client->getuName() + RPL_MOTD + client->getnName() + " :- Enjoy your stay!\r\n"); // 372 RPL_MOTD
    sendToClient(fd, ":" + client->getuName() + RPL_ENDOFMOTD + client->getnName() + " :End of /MOTD command.\r\n"); // 376 RPL_ENDOFMOTD

	sendToClient(fd, welcomeMessage);
	client->setisCapNegotiated(true);
	this->clientCount++;
}


int Server::checkReceived(std::string str, Client* cl) {

	if (cl->getisNick() == false) {
		sendToClient(cl->getfd(), ERR_NOTREGISTERED " :You have not registered\r\n");
		return 1;
	}
	std::vector<std::string> line = ::split(str, ' ');
	if (line[0].empty())
		return 1;
	if (line[0] == "JOIN")
		joinCMD(line, cl);
	else if (line[0] == "PART")
		partCMD(line, cl);
	else if (line[0] == "KICK")
		kickCMD(line, cl);
	else if (line[0] == "TOPIC")
		topicCMD(line, cl);
	else if (line[0] == "MODE")
		modeCMD(line, cl);
	else if (line[0] == "PRIVMSG")
		privCMD(line, cl);
	else if (line[0] == "INVITE")
		inviteCMD(line, cl);
	else if (line[0] == "QUIT") {
		quitCMD(line, cl);
		return 0;
	}
	else if (line[0] == "PING")
		pingCMD(line, cl);
	else
		sendToClient(cl->getfd(), ERR_UNKNOWNCOMMAND + line[0] + " :Unknown command\r\n");
	return 1;
}

void Server::addClient() {
	Client client;
	struct sockaddr_in cliadd;
	struct pollfd NewPoll;
	socklen_t len = sizeof(cliadd);

	int clientfd = accept(serverfd, (sockaddr *)&(cliadd), &len);

	if (clientfd == -1)
		{std::cout << "accept() failed" << std::endl; return;}

	if (fcntl(clientfd, F_SETFL, O_NONBLOCK) == -1) //-> set the socket option (O_NONBLOCK) for non-blocking socket
		{std::cout << "fcntl() failed" << std::endl; return;}
	
	// Check Maximum number of clients
	if (clientCount >= MAX_CLIENTS) {
		sendToClient(clientfd, ERR_UNAVAILRESOURCE ":Server is full, try again later.\r\n");
		close(clientfd);
		return;
	}

	NewPoll.fd = clientfd; //-> add the client socket to the pollfd
	NewPoll.events = POLLIN; //-> set the event to POLLIN for reading data
	NewPoll.revents = 0; //-> set the revents to 0

	client.setfd(clientfd); //-> set the client file descriptor
	client.setipAdd(inet_ntoa((cliadd.sin_addr))); //-> convert the ip address to string and set it
	clients.push_back(client); //-> add the client to the vector of clients
	pollfds.push_back(NewPoll); //-> add the client socket to the pollfd
}

void Server::removeClient(int fd) {
	
	for (size_t i = 0; i < clients.size(); ++i) {
		if (clients[i].getfd() == fd) {
			clients.erase(clients.begin() + i);
			break ;
		}
	}
	for (size_t i = 0; i < pollfds.size(); ++i){
		if (pollfds[i].fd == fd){
			pollfds.erase(pollfds.begin() + i);
			break ;
		}
	}
	std::map<int, std::string>::iterator it = clbuffer.find(fd);
	if (it != clbuffer.end())
		clbuffer.erase(it);
}

void Server::addChannel(std::string& chName, Client& cl) {
	
	if (check_ChanName(chName) && chName.size() <= MAX_CHAN_NAME_LEN) {
		Channel ch(chName, cl);

		channels.push_back(ch);
		sendToClient(cl.getfd(), ":" + cl.getnName() + "!~" + cl.getuName() + "@" + getHostname() + " JOIN :" + ch.getchannelName() + "\r\n");
		sendToClient(cl.getfd(), RPL_NAMREPLY + cl.getnName() + " = " + chName + " :@" + cl.getnName() + "\r\n");
		sendToClient(cl.getfd(), RPL_ENDOFNAMES + cl.getnName() + " " + chName + " :End of /NAMES list\r\n");
		std::cout << "Client " << cl.getuName() << " created a channel " << chName << std::endl; 
	} else {
		// send Invalid channel name ERR MSG
		sendToClient(cl.getfd(), ERR_BADCHANNAME + cl.getnName() + " " + chName + " :Invalid channel name\r\n");
	}
}

bool Server::isChannel(const std::string& chname) {
	for (size_t i = 0; i < channels.size(); ++i) {
		if (chname == channels[i].getchannelName())
			return true;
	}
	return false;
}



Client* Server::getClient(int fd) {
	for (size_t i = 0; i < this->clients.size(); ++i) {
		if (this->clients[i].getfd() == fd)
			return &this->clients[i];
	}
	return NULL;
}

Client* Server::getClient(const std::string& name){
	for (size_t i = 0; i < this->clients.size(); ++i){
		if (this->clients[i].getnName() == name)
			return &clients[i];
	}
	return NULL;
}

Channel* Server::getChannel(const std::string& chname) {
	for (size_t i = 0; i < this->channels.size(); ++i){
		if (this->channels[i].getchannelName() == chname)
			return &this->channels[i];
	}
	return NULL;
}

void Server::displayClient() {
	for (size_t i = 0; i < clients.size(); ++i) {
		std::cout << "Client nick name: " << clients[i].getnName() << std::endl;
		std::cout << "Client user name: " << clients[i].getuName() << std::endl;
		std::cout << "Client fd: " << clients[i].getfd() << std::endl;	
		std::cout << "Client ip: " << getHostname() << std::endl;
	}
}

void Server::displayChannel() {
	if (channels.size() < 1)
		return;
	std::vector<class Client> tmp = channels[0].getclientList();
	for (size_t i = 0; i < tmp.size(); ++i){
		std::cout << "Client user name: " << tmp[i].getuName() << std::endl;
	}
}

void Server::joinCMD(std::vector<std::string> line, Client* cl) {
	
	//process join join command we have to check in this function contents of the line to determine which constructor show
	if (line.size() == 2) {
		std::vector<std::string> chname = split(line[1], ',');
		for(size_t i = 0; i < chname.size(); ++i){
			joinChannel(chname[i], NULL, cl);
		}
	} else if (line.size() == 3) {
		// this case is for taking keys for channel
		std::vector<std::string> chname = ::split(line[1], ',');
		std::vector<std::string> keys = ::split(line[2], ',');
		for (size_t i = 0; i < chname.size(); ++i){
			if (i < keys.size())
				joinChannel(chname[i], keys[i].c_str(), cl);
			else
				joinChannel(chname[i], NULL, cl);
		}
	} else {
		sendToClient(cl->getfd(), ERR_NEEDMOREPARAMS + cl->getnName() + " JOIN :Not enough parameters\r\n");
	}
}

void Server::joinChannel(std::string chName, const char* key, Client* cl){

	if (check_ChanName(chName)){ // check if the channel name is valid
		//check if the channel is existing
		Channel *tmpch = getChannel(chName);
		if (tmpch) {
			//check flags if its possible to join
			if (tmpch->checkclientExist(cl)){
				sendToClient(cl->getfd(), ERR_ALREADYREGISTRED + cl->getnName() + " " + chName + " :You're already in that channel\r\n");
				return ;
			}
			if (!tmpch->joinFlags()){addclienttoChannel(tmpch, cl); return;}
			if ((tmpch->getclientFlag() && tmpch->getclientSize() < tmpch->getlimit()) || !tmpch->getclientFlag()){
				if ((tmpch->getinvFlag() || tmpch->getkeyFlag()) && tmpch->checkinvClient(cl)){
					addclienttoChannel(tmpch, cl);
					tmpch->removeInvite(cl);
				} else if (tmpch->getinvFlag() && !tmpch->checkinvClient(cl))
					sendToClient(cl->getfd(), ERR_INVITEONLYCHAN + cl->getnName() + " " + tmpch->getchannelName() + " :Cannot join channel (+i)\r\n");
				else if (tmpch->getkeyFlag() && key)
					joinPass(tmpch, key, cl);
				else if (tmpch->getkeyFlag() && !key)
					sendToClient(cl->getfd(), ERR_BADCHANNELKEY + cl->getnName() + " " + tmpch->getchannelName() + " :Cannot join channel (+k)\r\n");

			} else
				sendToClient(cl->getfd(), ERR_CHANNELISFULL + cl->getnName() + " " + tmpch->getchannelName() + " :Cannot join channel (+l)\r\n");
		} else
			addChannel(chName, *cl);
	} else {
		sendToClient(cl->getfd(), ERR_NOSUCHCHANNEL + cl->getnName() + " " + chName + " :No such channel\r\n");
	}
}

void Server::addclienttoChannel(Channel* chName, Client* cl){
	chName->addClient(*cl);
	sendToChannel(*chName, ":" + cl->getnName() + "!~" + cl->getuName() + "@" + getHostname() + " JOIN :" + chName->getchannelName() + "\r\n");

	sendToClient(cl->getfd(), ":" + cl->getnName() + "!~" + cl->getuName() + "@" + getHostname() + " JOIN :" + chName->getchannelName() + "\r\n");
	if (chName->getTopic().size() > 0)
		sendToClient(cl->getfd(), RPL_TOPIC + cl->getnName() + " " + chName->getchannelName() + " :" + chName->getTopic() + "\r\n");	
	std::vector<class Client> tmplist = chName->getclientList();			
	std::string clientListStr;
	for (size_t i = 0; i < tmplist.size(); ++i) {
		if (i > 0)
		clientListStr += " ";
		// Set @ for operator
		if (chName->checkclientOper(&tmplist[i]))
			clientListStr += "@";
		clientListStr += tmplist[i].getnName();
	}
	sendToClient(cl->getfd(), RPL_NAMREPLY + cl->getnName() + " = " + chName->getchannelName() + " :" + clientListStr + "\r\n");
	sendToClient(cl->getfd(), RPL_ENDOFNAMES + cl->getnName() + " " + chName->getchannelName() + " :End of /NAMES list\r\n");
}

void Server::joinPass(Channel* chName, const char* key, Client* cl){
	std::string tmpkey(key);
	if (chName->getclientFlag() == true && chName->getlimit() <= chName->getclientSize()) {
		sendToClient(cl->getfd(), ERR_CHANNELISFULL + cl->getnName() + " " + chName->getchannelName() + " :Cannot join channel (+l)\r\n");
		return ;
	}
	if (tmpkey == chName->getKey()) {
		if (chName->getTopic().size() > 0)
			sendToClient(cl->getfd(), RPL_TOPIC + cl->getnName() + " " + chName->getchannelName() + " :" + chName->getTopic() + "\r\n");

		std::vector<class Client> tmplist = chName->getclientList();
		std::string clientListStr;
		for (size_t i = 0; i < tmplist.size(); ++i) {
			if (i > 0) {
				clientListStr += " ";
			}
			clientListStr += tmplist[i].getnName();
		}
		sendToClient(cl->getfd(), RPL_NAMREPLY + cl->getnName() + " = " + chName->getchannelName() + " :" + clientListStr + "\r\n");
		sendToClient(cl->getfd(), RPL_ENDOFNAMES + cl->getnName() + " " + chName->getchannelName() + " :End of /NAMES list\r\n");
		chName->addClient(*cl);
	} else {
		sendToClient(cl->getfd(), ERR_BADCHANNELKEY + cl->getnName() + " " + chName->getchannelName() + " :Cannot join channel (+k)\r\n");
	}
}

void Server::partCMD(std::vector<std::string> line, Client* cl){
	if (line.size() > 1){
		std::vector<std::string> chname = ::split(line[1], ',');
		for (size_t i = 0; i < chname.size(); ++i){
			Channel* tmpch = getChannel(chname[i]);

			if (!tmpch){
				sendToClient(cl->getfd(), ERR_NOSUCHCHANNEL + cl->getnName() + " " + chname[i] + " :No such channel\r\n");
				continue;
			}
			if (!tmpch->checkclientExist(cl)){
				sendToClient(cl->getfd(), ERR_NOTONCHANNEL + cl->getnName() + " " + chname[i] + " :You're not on that channel\r\n");
				continue;
			}

			// Reason for leaving channel
			std::string reason;
			for (size_t i = 2; i < line.size(); ++i){
				if (i > 2)
					reason += " ";
				reason += line[i];
			}
			if (reason.empty())
				reason = " :Leaving channel\r\n";
			else
				reason = " :" + reason + "\r\n";

			// Remove client from channel
			tmpch->removeClient(cl);
			sendToClient(cl->getfd(), ":" + cl->getnName() + "!~" + cl->getuName() + "@" + getHostname() + " PART " + tmpch->getchannelName() + reason);

			// Broadcast to all clients in the channel
			std::vector<class Client> tmplist = tmpch->getclientList();
			for (size_t i = 0; i < tmplist.size(); ++i){
				if (tmplist[i].getfd() != cl->getfd())
					sendToClient(tmplist[i].getfd(), ":" + cl->getnName() + "!~" + cl->getuName() + "@" + getHostname() + " PART " + tmpch->getchannelName() + reason);
			}
			// Delete channel if no clients are in it
			if (tmpch->getclientSize() == 0) {
				for (size_t i = 0; i < channels.size(); ++i) {
					if (channels[i].getchannelName() == tmpch->getchannelName())
						channels.erase(channels.begin() + i);
				}
			}
		}
	}
	else
		sendToClient(cl->getfd(), ERR_NEEDMOREPARAMS + cl->getnName() + " PART :Not enough parameters\r\n");
}

void Server::kickCMD(std::vector<std::string> line, Client *cl){
	if (line.size() > 2){
		std::string reason = " :Client has been kicked out of the channel\r\n";
		if (line.size() > 3){
			const char* tmp = line[3].c_str();
			if (tmp[0] == ':')
				reason = addStrings(line, 3);
		}
		Channel* tmpch = getChannel(line[1]);
		std::vector<std::string> target = ::split(line[2], ',');

		if (!tmpch)
			sendToClient(cl->getfd(), ERR_NOSUCHCHANNEL + cl->getnName() + " " + line[1] + " :No such channel\r\n");
		else if (!tmpch->checkclientExist(cl))
			sendToClient(cl->getfd(), ERR_NOTONCHANNEL + cl->getnName() + " " + line[1] + " :You're not on that channel\r\n");
		else if (!tmpch->checkclientOper(cl))
			sendToClient(cl->getfd(), ERR_CHANOPRIVSNEEDED + cl->getnName() + " " + line[1] + " :You're not channel operator\r\n");
		else {
			for (size_t i = 0; i < target.size(); ++i){
				if (target[i].empty())
					continue;
				Client *tmpcl = getClient(target[i]);
				
				if (!tmpcl || !tmpch->checkclientExist(tmpcl)){
					sendToClient(cl->getfd(), ERR_USERNOTINCHANNEL + cl->getnName() + " " + target[i] + " " + line[1] + " :They aren't on that channel\r\n");
					continue;
				}

				tmpch->removeClient(tmpcl);
				if (reason.empty()){
					sendToClient(tmpcl->getfd(), ":" + cl->getnName() + " KICK " + tmpch->getchannelName() + " " + tmpcl->getnName() + " :You have been kicked from the channel\r\n");
					sendToChannel(*tmpch, ":" + cl->getnName() + " KICK " + tmpch->getchannelName() + " " + tmpcl->getnName() + reason);
				}
				else{
					sendToClient(tmpcl->getfd(), ":" + cl->getnName() + " KICK " + tmpch->getchannelName() + " " + tmpcl->getnName() + " " + reason.c_str());
					sendToChannel(*tmpch, ":" + cl->getnName() + " KICK " + tmpch->getchannelName() + " " + tmpcl->getnName() + " " + reason.c_str());
				}

				// Delete channel if no clients are in it
				if (tmpch->getclientSize() == 0) {
					for (size_t i = 0; i < channels.size(); ++i) {
						if (channels[i].getchannelName() == tmpch->getchannelName())
							channels.erase(channels.begin() + i);
					}
				}
			}
		}
	}
	else
		sendToClient(cl->getfd(), ERR_NEEDMOREPARAMS + cl->getnName() + " KICK :Not enough parameters\r\n");
}


void Server::privCMD(std::vector<std::string> line, Client* cl){

	if (line.size() > 2) {
		const char* tmp = line[2].c_str();
		if (tmp[0] != ':') {
			std::cerr << "Invalid msg param" << std::endl;
			return ;
		}
		std::vector<std::string> target = ::split(line[1], ',');
		std::string tosend = addStrings(line, 2);
		for (size_t i = 0; i < target.size(); ++i){
			tmp = target[i].c_str();
			if (tmp[0] == '#'){
				Channel *tmpch = getChannel(target[i]);
				if (tmpch && tmpch->checkclientExist(cl))
					privCMDsendtoChannel(tmpch, cl, tosend);
				else if (tmpch){
					sendToClient(cl->getfd(), ERR_NOTONCHANNEL + tmpch->getchannelName() + " :Cannot send to channel\r\n");
				} else
					sendToClient(cl->getfd(), ERR_NOSUCHCHANNEL + target[i] + " :No such channel\r\n");
				continue ;
			}
			Client* receiver = getClient(target[i]);
			if (receiver == NULL || cl == NULL)
				continue ;
			sendToClient(receiver->getfd(), cl->getnName() + " PRIVMSG " + receiver->getnName() + " " + tosend);
		}
	} else {
		sendToClient(cl->getfd(), ERR_NEEDMOREPARAMS " * PRIVMSG :Not enough parameters\r\n");
	}
}


void Server::privCMDsendtoChannel(Channel* ch, Client* cl, std::string tosend){
	if (ch == NULL || cl == NULL)
		return ;
	std::vector<class Client> tmplist = ch->getclientList();
	for (size_t i = 0; i < tmplist.size(); ++i){
		if (cl->getfd() == tmplist[i].getfd())
			continue ;
		sendToClient(tmplist[i].getfd(), ":" + cl->getnName() + "!" + cl->getnName() + "@" + getHostname() + " PRIVMSG " + ch->getchannelName() + " " + tosend);
	}
}

void Server::modeCMD(std::vector<std::string> line, Client* cl){

	// We are only requered to implement channel modes.
	// if Mode is called for non channel (on client), return.
	if (line.size() >= 2){
		Channel* ch = getChannel(line[1]);
		if (ch == NULL){
			sendToClient(cl->getfd(), ERR_NOSUCHCHANNEL + cl->getnName() + " " + line[1] + " :No such channel\r\n");
			return ;
		}
		if (line.size() == 2){
			sendToClient(cl->getfd(), RPL_CHANNELMODEIS + cl->getnName() + " " + ch->getchannelName() + " +" + ch->getMode() + "\r\n");
			return ;
		}
		if (!ch->checkclientOper(cl)){
			sendToClient(cl->getfd(), ERR_CHANOPRIVSNEEDED + ch->getchannelName() + " :You're not channel operator\r\n");
			return ;
		}
		const char* modestring = line[2].c_str();
		char c = '\0';
		size_t param = 3;
		if (modestring[0] == '\0') {
			sendToClient(cl->getfd(), RPL_CHANNELMODEIS + cl->getnName() + " " + ch->getchannelName() + " " + ch->getMode() + "\r\n");
			return ;
		}
		if (modestring[0] != '+' && modestring[0] != '-'){
			sendToClient(cl->getfd(), ERR_UNKNOWNMODE + line[2] + " :is unknown mode char to me\r\n");
			return ;
		}
		for (size_t i = 0; modestring[i]; ++i){
			if (modestring[i] == '-' || modestring[i] == '+')
				c = modestring[i];
			else {
				switch(modestring[i]){
					case 't':
						if (ch->settopicFlag(c))
							sendToChannel(*ch,RPL_CHANNELMODEIS + cl->getnName() + " " + ch->getchannelName() + " +" + ch->getMode() + "\r\n");
						break ;
					case 'i':
						if (ch->setinvFlag(c)) {
							sendToChannel(*ch, ":" + cl->getnName() + " INVITE " + ch->getchannelName() + " :" + ch->getMode() + "\r\n");
							ch->setinvFlag(c);
						}
						break ;
					case 'k':
						if (c == '-' && ch->getkeyFlag()){ch->setkeyFlag(false);break;}
						if (param >= line.size()) break;
						setChannelKey(ch, cl, line[param]); param++; break;
					case 'o':
						if (param >= line.size())
							break ;
						ch->setClientOper(getClient(line[param]), c);
						ch->displayoper();
						param++;
						break ;
					case 'l':
						if (c == '-'){ch->setclientFlag(false);break;}
						if (param >= line.size()) break;
						setChannelLimit(ch, cl,line[param]); param++; break;
				}
			}
		}
	}
	else {
		sendToClient(cl->getfd(), ERR_NEEDMOREPARAMS " * MODE :Not enough parameters\r\n");
	}
}

void Server::inviteCMD(std::vector<std::string> line, Client* cl){
	if (line.size() == 3){
		Client* tmp = getClient(line[2]);
		Channel* tmpch = getChannel(line[1]);

		if (!tmpch){
			sendToClient(cl->getfd(), ERR_NOSUCHCHANNEL + cl->getnName() + " " + tmpch->getchannelName() + "\r\n");
			return;
		}
		if (!tmp){
			sendToClient(cl->getfd(), ERR_NOSUCHNICK  ":No such nick/channel\r\n");
			return ;
		}
		if (tmpch->checkclientExist(cl) && tmpch->checkclientOper(cl)){
			if (tmpch->checkclientExist(tmp)){
				sendToClient(cl->getfd(), ERR_USERONCHANNEL + cl->getnName() + " " + tmp->getnName() + " " + tmpch->getchannelName() + " :is already on channel\r\n");
				return ;
			}
			else {
				//Client should be invited not joined, have to add channel to the clients invited list so it could bypass keys and such in
				tmpch->invClient(tmp);
				sendToClient(tmp->getfd(), ":" + cl->getnName() + "!" + getHostname() + " INVITE " + tmp->getnName() + " " + tmpch->getchannelName() + "\r\n");
			}
		}
	} else {
		sendToClient(cl->getfd(), ERR_NEEDMOREPARAMS " * INVITE :Not enough parameters\r\n");
	}
}

int Server::setChannelLimit(Channel* chName, Client *cl, std::string str){
	char *ptr;
	long num = std::strtol(str.c_str(), &ptr, 10);

	if (num <= 0 || num > INT_MAX) {
		sendToClient(cl->getfd(), "Client limit amount exceeds INT_MAX\r\n");
		return -1;
	}
	else {
		chName->setLimit(num);
		chName->setclientFlag(true);
		return 0;
	}
}

void Server::setChannelKey(Channel* ch, Client* cl, std::string str){
	//check str for alphanumerical and empty.
	if(!checkpass(str)) {
		sendToClient(cl->getfd(), ERR_INVALIDKEY + cl->getnName() + " " + ch->getchannelName() + " :Invalid channel key\r\n");
		return ;
	}
	ch->setKey(str);
	ch->setkeyFlag(true);

}

void Server::pingCMD(std::vector<std::string> line, Client* cl){
	if (line.size() < 2){
		sendToClient(cl->getfd(), ERR_NEEDMOREPARAMS " * PING :Not enough parameters\r\n");
		return ;
	} else if (line.size() > 2){
		sendToClient(cl->getfd(), ERR_NOORIGIN  "PING :No origin specified\r\n");
		return ;
	} else if (line[1].empty()){
		sendToClient(cl->getfd(), ERR_NOORIGIN "PING :No origin specified\r\n");
		return ;
	}
	if (cl->getisRegistered() == false) {
		sendToClient(cl->getfd(), ERR_NOTREGISTERED ":You have not registered\r\n");
		return ;
	}

	sendToClient(cl->getfd(), "PONG " + line[1] + "\r\n");
}

void Server::quitCMD(std::vector<std::string> line, Client* cl){
	// (void)line;
	int fd = cl->getfd();
	if (cl->getisRegistered() == false) {
		sendToClient(fd, ERR_NOTREGISTERED ":You have not registered\r\n");
		std::cerr << "Client [" << fd << "] has not registered" << std::endl;
		return ;
	}

	// quit message from client
	std::string quitMessage;
	for (size_t i = 1; i < line.size(); ++i) {
		quitMessage += line[i];
		if (i < line.size() - 1)
			quitMessage += " ";
	}

	// Send quit message to all channels client was in
	std::vector<Channel> chList = channels;
	for (size_t i = 0; i < chList.size(); ++i) {
		if (chList[i].checkclientExist(cl))
			sendToChannel(chList[i], ":" + cl->getnName() + "!~" + cl->getuName() + "@" + getHostname() + " PART :" + chList[i].getchannelName() + "\r\n");
	}

	std::cout << "Client " << cl->getnName() << " " << quitMessage << std::endl;
	this->clientCount--;
	eraseClient(cl);  // removes client from all its existing channels
	removeClient(fd);
	close(fd);
}

void Server::sendCapabilities(int fd) {

    std::vector<std::string> serverCapabilities;
    serverCapabilities.push_back("TLS");
    serverCapabilities.push_back("UTF8_ONLY");
    serverCapabilities.push_back("CHANNEL_MODES");
	serverCapabilities.push_back("multi-prefix");
	serverCapabilities.push_back("server-time");

    std::string capabilityList = "CAP * LS :";
    for (std::vector<std::string>::const_iterator it = serverCapabilities.begin(); it != serverCapabilities.end(); ++it) {
        capabilityList += *it + " ";
    }

    capabilityList += "\r\n";

    ssize_t sentBytes = send(fd, capabilityList.c_str(), capabilityList.length(), 0);
    if (sentBytes == -1) {
        // Handle send error
        std::cerr << "Error sending capabilities to client [" << fd << "]" << std::endl;
        removeClient(fd);
        close(fd);
    } else if (sentBytes < (ssize_t)capabilityList.length()) {
        // Handle partial send
        std::cerr << "Partial send of capabilities to client [" << fd << "]" << std::endl;
    }

}


void Server::clAuthentication(int fd, std::vector<std::string>& vec) {
	bool isCap = false;
	Client *client = getClientByFd(fd);

	if (vec.empty()) {
		return ;
	}

	for (size_t i = 0; i < vec.size(); i++) {
		if (vec[0] ==  "CAP")
			isCap = true;
		if (vec[0] == "PASS" || vec[0] == "pass")
			passCMD(fd, vec, isCap);
		else if (vec[0] == "NICK" || vec[0] == "nick") 
			nickCMD(fd, vec, isCap);
		else if (vec[0] == "USER" || vec[0] == "user") 
			userCMD(fd, vec);
		else if (vec[0] == "CAP" || vec[0] == "cap")
			capCMD(client, vec, fd);

		vec.erase(vec.begin());
		if (vec.empty()) {
			break;
		}
		vec.erase(vec.begin());
	}
	return ;
}

// Handle the CAP command from the client. 
void Server::capCMD(Client* client, std::vector<std::string>& vec, int fd) {
	if (vec[1] == "LS" || vec[1] == "ls") {
		if (client->getisCapNegotiated() == true) {
			return ;
		}
		sendCapabilities(fd); // set the client info. Send the capabilities to the client.
	} else if (vec[1] == "REQ" || vec[1] == "req") {
		sendToClient(fd, "CAP * ACK " + vec[2] + "\r\n");
		vec.erase(vec.begin()); // remove the CAP command. This is crucial for the client to be able to process the rest of the command (Especially for the NICK command, if nickname already exists, the client will not be able to change it without the CAP command being removed from the command vector)
	} else if (vec[1] == "END" || vec[1] == "end") {
		
	}
	 else {
		sendToClient(fd, ERR_UNKNOWNCOMMAND " * :Unknown CAP command\r\n");
	}
}

// Handle the PASS command from the client. Remove the client if the password is incorrect.
void Server::passCMD(int fd, const std::vector<std::string>& vec, bool isCap) {
	if (getClientByFd(fd)->getisPass() == true) {
		sendToClient(fd, ERR_ALREADYREGISTRED " * :You may not reregister\r\n");
		// std::cerr << "Client [" << fd << "] may not reregister" << std::endl;
		return ;
	}

	if (vec.size() < 2 || vec[1].empty()) {
		sendToClient(fd, ERR_NEEDMOREPARAMS " * PASS :Not enough parameters\r\n");
		// std::cerr << "Invalid PASS command format from client [" << fd << "]" << std::endl;
		return ;
	}

	if (vec.size() != 2) {
		if (isCap == false) {
			sendToClient(fd, ERR_NEEDMOREPARAMS " * PASS :Not enough parameters\r\n");
			// std::cerr << "Invalid PASS command format from client [" << fd << "]" << std::endl;
			return ;
		}
	}

	if (vec[1] == this->pass) {
		getClientByFd(fd)->setisPass(true);
		// std::cout << "Password is correct for client [" << fd << "]" << std::endl;
		return ;
	} else {
		sendToClient(fd, ERR_PASSWDMISMATCH " * :Password incorrect\r\n");
		// std::cerr << "Password is incorrect for client [" << fd << "]" << std::endl;
		return ;
	}
}


bool Server::isNickValid(const std::string& nick) { // May want to make nickname limit for 9 chars
	if (nick.empty() || nick[0] == '\0') // check if the nickname is empty
		return false;
	if (nick.length() > 30) // check if the nickname is too long. The maximum length is 30 characters(There is no official limit, used for testing purposes).
		return false;
	if (nick[0] == '#' || isdigit(nick[0]) || nick[0] == ':') // check if the nickname starts with a disallowed character (: is OK with irssi)
		return false;
	for (size_t i = 0; i < nick.length(); i++) {
        if (!isalnum(nick[i]) && !strchr("[]{}\\|-_", nick[i]))  // check if the nickname contains disallowed characters PS. NEED TO ADD MORE DISALLOWED CHARACTERS
			return false;
    }
	if (nick.size() >= MAX_NICK_LENGTH)
		return false;
	return true;
}

// Handle the NICK command from the client. Set the nickname for the client.
// Send an error message if the nickname is already in use. irssi out automatically set the nickname to a different but similar one.
void Server::nickCMD(int fd, const std::vector<std::string>& vec, bool isCap) {
	// (void)isCap;
	if (getClientByFd(fd)->getisPass() == false) {
		sendToClient(fd, ERR_NOTREGISTERED ":You have not registered\r\n");
		// std::cerr << "Client [" << fd << "] has not registered" << std::endl;
		return ;
	}

	if (vec.size() < 2) {
		sendToClient(fd, ERR_NONICKNAMEGIVEN " * :No nickname given\r\n");
		// std::cerr << "Invalid NICK command format from client [" << fd << "]" << std::endl;
		return;
	}

	if (vec.size() != 2) {
		if (isCap == false) {
			sendToClient(fd, ERR_NEEDMOREPARAMS " * NICK :Not enough parameters\r\n");
			// std::cerr << "Invalid NICK command format from client [" << fd << "]" << std::endl;
			return ;
		}
	}

	if (!isNickValid(vec[1])) {
		sendToClient(fd, ERR_ERRONEUSNICKNAME + vec[1] + " :Erroneous Nickname\r\n");
		// std::cout << "Error: Invalid nickname" << std::endl;
		return ;
	}

	Client *client = getClientByFd(fd);

	for (size_t i = 0; i < clients.size(); i++) {
		if (clients[i].getnName() == vec[1]) {
			getClientByFd(fd)->setisNick(true);
			sendToClient(fd, ERR_NICKNAMEINUSE + vec[1] + " :Nickname is already in use\r\n");
			return ;
		}
	}
	// send new client nick name to client
	if (client->getisNick() == true) {
		sendToClient(fd, ":" + client->getnName() + "!" + client->getuName() + "@" + getHostname() + " NICK :" + vec[1] + "\r\n");

		// Broadcast to channels that client has updated thier nick name
		std::vector<Channel> chList = channels;
		for (size_t i = 0; i < chList.size(); ++i) {
			if (chList[i].checkclientExist(client)) {
				for (int j = 0; j < chList[i].getclientSize(); ++j) {
					if (chList[i].getclientList()[j].getfd() != fd)
						sendToClient(chList[i].getclientList()[j].getfd(), ":" + client->getnName() + " NICK :" + vec[1] + "\r\n");
				}
			}
		}

	}

	std::string oldNick = client->getnName();
	client->setnName(vec[1]);
	client->setisNick(true);

	// std::cout << "Nickname set to [" << vec[1] << "] for client [" << fd << "]" << std::endl;
}


// Handle the USER command from the client. Set the username for the client.
void Server::userCMD(int fd, const std::vector<std::string>& vec) {

	if (getClientByFd(fd)->getisPass() == false || getClientByFd(fd)->getisNick() == false) {
		sendToClient(fd, ERR_NOTREGISTERED ":You have not registered\r\n");
		return ;
	} else if (getClientByFd(fd)->getisRegistered() == true) {
		sendToClient(fd, ERR_ALREADYREGISTRED " *" + getClientByFd(fd)->getnName() + " :You may not reregister\r\n");
	}
	
	if (vec.size() < 5) {
		sendToClient(fd, ERR_NEEDMOREPARAMS " * USER :Not enough parameters\r\n");
		return;
	}

	if (!vec[1].empty()) {
		Client *client = getClientByFd(fd);
		
		client->setuName(vec[1]);
		client->setservName(vec[3]);
		client->setrealName(vec[4]);
		client->setisRegistered(true);
	}
	else {
		sendToClient(fd, ERR_NEEDMOREPARAMS " * USER :Not enough parameters\r\n");
	}
}


// Send a message to the client. Return the number of bytes sent.
ssize_t Server::sendToClient(int fd, const std::string& msg) {
	ssize_t sentBytes = send(fd, msg.c_str(), msg.length(), 0);
	if (sentBytes == -1) {
		std::cerr << "Error sending message to client [" << fd << "]" << std::endl;
	} else if (sentBytes < (ssize_t)msg.length()) {
		std::cerr << "Partial send of message to client [" << fd << "]" << std::endl;
	} else {
		// std::cout << "Sent message to client [" << fd << "]" << std::endl;
	}
	return sentBytes;
}

void Server::sendToChannel(Channel& ch, const std::string& msg){
	
	if (isChannel(ch.getchannelName())){
		std::vector<class Client> tmp = ch.getclientList();
		for (size_t i = 0; i < tmp.size(); ++i){
			sendToClient(tmp[i].getfd(), msg);
		}
	}
}

// Split the command string into a vector of strings. Clears empty strings and returns the vector.
std::vector<std::string> Server::splitCmd(const std::string& str) {
	std::vector<std::string> vec;
	std::string tmp;
	for (size_t i = 0; i < str.length(); i++) {

		if (str[i] == ' ' || str[i] == '\n' || str[i] == '\r') {
			vec.push_back(tmp);
			tmp.clear();
		} else {
			tmp += str[i];
		}
	}
	if (!tmp.empty())
		vec.push_back(tmp);
	// Remove empty strings from the vector.
	for (std::vector<std::string>::iterator it = vec.begin(); it != vec.end();) {
		if (it->empty()	|| *it == "\r" || *it == "\n") {
			it = vec.erase(it); // erase returns the next valid iterator
		} else {
			++it; // move to the next element
		}
	}

	return vec;
}

// Get the client object using the file descriptor.
Client* Server::getClientByFd(int fd) {
	for (size_t i = 0; i < clients.size(); i++) {
		if (clients[i].getfd() == fd) {
			return &clients[i];
		}
	}
	return NULL;
}


std::string Server::addStrings(std::vector<std::string> lines, size_t i) {
	std::string tmp;
	for (size_t j = i; j < lines.size(); ++j){
		tmp += lines[j];
		tmp += " ";
	}
	tmp += "\r\n";
	return tmp;
}

void sigHandler(int signum) {
	(void)signum;
	::stopflag = true;
	std::cout << "\nServer is shutting down" << std::endl;
}

void Server::topicCMD(std::vector<std::string>& vec, Client *cl) {
	if (vec.size() >= 2) {
		Channel* tmpch = getChannel(vec[1]);

		if (tmpch &&!tmpch->checkclientExist(cl)) {
			if (tmpch->gettopicFlag() == false) {
				sendToClient(cl->getfd(), ERR_NOTONCHANNEL + vec[1] + " :You're not on that channel\r\n");
				std::cerr << "Client is not in the channel for client [" << cl->getfd() << "]" << std::endl;
				return ;
			}
		}

		if (vec.size() == 2 && tmpch != NULL){
			sendToClient(cl->getfd(), RPL_TOPIC + tmpch->getchannelName() + ":" + tmpch->getTopic() + "\r\n");
		} else if (vec.size() > 2 && tmpch != NULL) {
			// if client a channel operator
			if (!tmpch->checkclientOper(cl)) {
				// check channel flag for topic
				if (tmpch->gettopicFlag() == true) {
					sendToClient(cl->getfd(), ERR_CHANOPRIVSNEEDED + cl->getnName() + " " + tmpch->getchannelName() + " :You're not channel operator\r\n");
					std::cerr << "Client is not a channel operator for client [" << cl->getfd() << "]" << std::endl;
					return ;
				}
			}

			std::string newTopic = vec[2];
			for (size_t i = 3; i < vec.size(); i++) {
				newTopic += " " + vec[i];
			}

			tmpch->setTopic(newTopic);
			for (size_t i = 0; i < tmpch->getclientList().size(); i++) {
				sendToClient(tmpch->getclientList()[i].getfd(), ":" + cl->getnName() + " TOPIC " + tmpch->getchannelName() + " " + newTopic + "\r\n");
			}
			
		} else {
			sendToClient(cl->getfd(), ERR_NOSUCHCHANNEL + cl->getnName() + " " + vec[1] + " : No such channel\r\n");
			std::cerr << "Channel does not exist for client [" << cl->getfd() << "]" << std::endl;
			return ;
		}
	} else {
		sendToClient(cl->getfd(), ERR_NEEDMOREPARAMS "* TOPIC :Not enough parameters\r\n");
		std::cerr << "Invalid TOPIC command format from client [" << cl->getfd() << "]" << std::endl;
		return ;
	}
	return ;
}
