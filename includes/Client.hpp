#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include "Channel.hpp"
#include "Errormsg.hpp"

class Channel;

class Client {
    private:
        int             fd; // file descriptor for client
        bool            operFlag; // operator flag for the commands
        std::string     nName; // nickname
        std::string     uName; // username 
        std::string     ipAdd; // ip address
		std::vector<class Channel> clientChannelList;
		std::string		currentchannel;
    public:
        Client();
        ~Client();

        // getter functions && setter functions//
        int     getfd() const;
        bool    getoperFlag() const;
        std::string getnName() const;
        std::string getuName() const;
        std::string getipAdd() const;
		std::vector<class Channel> getlist() const;

		void setfd(int fd);
		void setoperFlag(bool flag);
		void setnName(const std::string& str);
		void setuName(const std::string& str);
		void setipAdd(const std::string& str);
        // ------------------//
		// std::removeChannel(const Channel *ptr)

		/* was thinking of making handling operator = for Client just to make shallow copy and have a bool operator == for easily finding Client inside vector */
		bool operator==(const Client& tocheck);
		void addChannel(const Channel& ch);
};

#endif