#include "stdafx.h"
#include "crypto.h"
#include "main.h"
#include "utils.h"

const int portListener = 4826;
using boost::system::error_code;

const char* privatekeyFile = ".privatekey";

net::io_service io_service;

std::list<int> ports;
extern std::string e0str;

modes mode = CENTER;

int newPort()
{
	if (ports.empty())
		return -1;
	std::list<int>::iterator portItr = ports.begin();
	for (int i = std::rand() % ports.size(); i > 0; i--)
		portItr++;
	int port = *portItr;
	ports.erase(portItr);
	return port;
}

void freePort(unsigned short port)
{
	ports.push_back(port);
}

void server::start()
{
	accepting = std::make_shared<net::ip::tcp::socket>(io_service);
	acceptor.async_accept(*accepting,
		[this](boost::system::error_code ec){
		accept(ec);
	});
}

void server::accept(error_code ec)
{
	if (!ec)
	{
		int port = newPort();
		if (port == -1)
			std::cerr << "Socket:No port available" << std::endl;
		else
		{
			net::ip::tcp::endpoint localAddr(net::ip::tcp::v4(), port);
			pre_sessions.emplace(std::make_shared<pre_session>(io_service, localAddr, this));

			socket_ptr accepted(accepting);
			unsigned short port_send = static_cast<unsigned short>(port);
			const int send_size = sizeof(unsigned short) / sizeof(char);
			char* send_buf = new char[send_size];
			memcpy(send_buf, reinterpret_cast<char*>(&port_send), send_size);
			net::async_write(*accepted,
				net::buffer(send_buf, send_size),
				[accepted, send_buf](boost::system::error_code ec, std::size_t length)
			{
				delete[] send_buf;
				accepted->close();
			}
			);
		}
	}

	start();
}

void server::ping()
{
	timer.expires_from_now(boost::posix_time::seconds(5));
	timer.async_wait([this](boost::system::error_code ec){
		if (!ec)
			send(nullptr, std::string("\0", 1));
		ping();
	});
}

void server::join(std::shared_ptr<session> _user)
{
	std::cout << "New user " << _user->get_address() << std::endl;
	sessions.emplace(_user);
}

void server::leave(std::shared_ptr<session> _user)
{
	std::cout << "Delete user " << _user->get_address() << std::endl;
	sessions.erase(_user);
	if (mode != CENTER || _user->get_state() == session::LOGGED_IN)
		send_message(nullptr, "Delete user " + _user->get_address());
}

bool server::login(const std::string &name, const std::string &passwd)
{
	userList::iterator itr = users.find(name);
	if (itr == users.end())
		return false;
	std::string hashed_passwd;
	calcSHA512(passwd, hashed_passwd);
	if (itr->second.passwd == hashed_passwd)
		return true;
	return false;
}

void server::send_message(std::shared_ptr<session> from, const std::string& msg)
{
	std::string sendMsg;
	if (from != nullptr)
		sendMsg = from->get_address() + ":" + msg;
	else
		sendMsg = msg;
	sessionList::iterator itr = sessions.begin(), itrEnd = sessions.end();
	for (; itr != itrEnd; itr++)
		if (*itr != from && (mode != CENTER || (*itr)->get_state() == session::LOGGED_IN))
			(*itr)->send_message(sendMsg);
}

void server::send(std::shared_ptr<session> from, const std::string& data)
{
	sessionList::iterator itr = sessions.begin(), itrEnd = sessions.end();
	for (; itr != itrEnd; itr++)
		if (*itr != from && (mode != CENTER || (*itr)->get_state() == session::LOGGED_IN))
			(*itr)->send(data);
}

bool server::process_command(std::string command, user::group_type group)
{
	trim(command);
	std::string section;
	while (!(command.empty() || isspace(command.front())))
	{
		section.push_back(command.front());
		command.erase(0, 1);
	}
	command.erase(0, 1);
	if (section == "op")
	{
		if (group == user::ADMIN)
		{
			userList::iterator itr = users.find(command);
			if (itr != users.end())
				itr->second.group = user::ADMIN;
			io_service.post([this](){
				write_config();
			});
		}
		else
			return false;
	}
	else if (section == "reg")
	{
		if (group == user::ADMIN)
		{
			section.clear();
			while (!(command.empty() || isspace(command.front())))
			{
				section.push_back(command.front());
				command.erase(0, 1);
			}
			command.erase(0, 1);
			std::string hashed_passwd;
			calcSHA512(command, hashed_passwd);

			userList::iterator itr = users.find(section);
			if (itr == users.end())
			{
				users.emplace(section, user(section, hashed_passwd, user::USER));
				io_service.post([this](){
					write_config();
				});
				return true;
			}
			return false;
		}
		else
			return false;
	}
	else if (section == "unreg")
	{
		if (group == user::ADMIN)
		{
			userList::iterator itr = users.find(command);
			if (itr != users.end())
			{
				users.erase(itr);
				io_service.post([this](){
					write_config();
				});
				return true;
			}
			return false;
		}
		else
			return false;
	}
	else if (section == "stop")
	{
		if (group == user::ADMIN)
		{
			io_service.stop();
			std::thread stop_thread([this](){
				while (!io_service.stopped());
				std::exit(EXIT_SUCCESS);
			});
			stop_thread.detach();
		}
	}
	return true;
}

void server::read_config()
{
	if (!fs::exists(config_file))
	{
		write_config();
		return;
	}
	std::ifstream fin(config_file, std::ios_base::in | std::ios_base::binary);

	size_t userCount, size;
	fin.read(reinterpret_cast<char*>(&userCount), sizeof(size_t));
	char passwd_buf[64];
	for (; userCount > 0; userCount--)
	{
		user usr;
		fin.read(reinterpret_cast<char*>(&size), sizeof(size_t));
		char* buf = new char[size];
		fin.read(buf, size);
		usr.name = std::string(buf, size);
		delete[] buf;
		fin.read(passwd_buf, 64);
		usr.passwd = std::string(passwd_buf, 64);
		fin.read(reinterpret_cast<char*>(&size), sizeof(size_t));
		usr.group = static_cast<user::group_type>(size);
		users.emplace(usr.name, usr);
	}
}

void server::write_config()
{
	std::ofstream fout(config_file, std::ios_base::out | std::ios_base::binary);
	if (!fout.is_open())
		return;
	size_t size = users.size();
	fout.write(reinterpret_cast<char*>(&size), sizeof(size_t));
	std::for_each(users.begin(), users.end(), [&size, &fout](const std::pair<std::string, user> &pair){
		const user &usr = pair.second;
		size = usr.name.size();
		fout.write(reinterpret_cast<char*>(&size), sizeof(size_t));
		fout.write(usr.name.data(), size);
		fout.write(usr.passwd.data(), 64);
		size = static_cast<size_t>(usr.group);
		fout.write(reinterpret_cast<char*>(&size), sizeof(size_t));
	});
}

void print_usage()
{
	std::cout << "Usage:" << std::endl;
	std::cout << "\tmessenger_server [mode=relay|center]" << std::endl;
}

int main(int argc, char *argv[])
{
#ifdef NDEBUG
	try
	{
#endif
		for (int i = 1; i < argc; i++)
		{
			std::string arg(argv[i]);
			if (arg.substr(0, 5) == "mode=")
			{
				arg.erase(0, 5);
				if (arg == "center" || arg == "centre")
					mode = CENTER;
				else if (arg == "relay")
					mode = RELAY;
				else
				{
					print_usage();
					return 0;
				}
			}
			else
			{
				print_usage();
				return 0;
			}
		}
		for (int i = 5001; i <= 10000; i++)
			ports.push_back(i);
		std::srand(static_cast<unsigned int>(std::time(NULL)));

		if (fs::exists(privatekeyFile))
			initKey();
		else
			genKey();

		e0str = getPublicKey();
		unsigned short e0len = static_cast<unsigned short>(e0str.size());
		e0str = std::string(reinterpret_cast<const char*>(&e0len), sizeof(unsigned short) / sizeof(char)) + e0str;

		server server(io_service, net::ip::tcp::endpoint(net::ip::tcp::v4(), portListener));
		std::thread input_thread([&](){
			std::string command;
			while (true)
			{
				std::getline(std::cin, command);
				server.process_command(command, user::ADMIN);
			}
		});
		input_thread.detach();

		io_service.run();
#ifdef NDEBUG
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << "\n";
	}
#endif
	return 0;
}
