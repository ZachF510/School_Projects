// $Id: cix.cpp,v 1.2 2015-05-12 18:59:40-07 - - $
// Zachary Friedrich and Michael Simpson

#include <iostream>
#include <sstream> //added
#include <fstream> //added
#include <string>
#include <vector>
#include <unordered_map>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>

#include "protocol.h"
#include "logstream.h"
#include "sockets.h"

logstream log (cout);
struct cix_exit: public exception {};

string trim (const string &str) {
   size_t first = str.find_first_not_of(" \t");
   if (first == string::npos) return "";
   size_t last = str.find_last_not_of(" \t");
   return str.substr(first, last - first + 1);
}

unordered_map<string,cix_command> command_map {
   {"exit", CIX_EXIT},
   {"help", CIX_HELP},
   {"ls"  , CIX_LS  },
   {"get" , CIX_GET },
   {"put" , CIX_PUT },
   {"rm"  , CIX_RM  },
};

void cix_help() {
   static vector<string> help = {
      "exit         - Exit the program.  Equivalent to EOF.",
      "get filename - Copy remote file to local host.",
      "help         - Print help summary.",
      "ls           - List names of files on remote server.",
      "put filename - Copy local file to remote host.",
      "rm filename  - Remove file from remote server.",
   };
   for (const auto& line: help) cout << line << endl;
}

void cix_ls (client_socket& server) {
   cix_header header;
   header.command = CIX_LS;
   log << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   log << "received header " << header << endl;
   if (header.command != CIX_LSOUT) {
      log << "sent CIX_LS, server did not return CIX_LSOUT" << endl;
      log << "server returned " << header << endl;
      log << "ls: " << strerror(header.nbytes) << endl;
   }else {
      char buffer[header.nbytes + 1];
      recv_packet (server, buffer, header.nbytes);
      log << "received " << header.nbytes << " bytes" << endl;
      buffer[header.nbytes] = '\0';
      cout << buffer;
   }
}

void cix_get (client_socket& server, string& filename) {
   cix_header header;
   header.command = CIX_GET;
   if(filename.size() >= FILENAME_SIZE) {
      log << "get: " << filename << ": filename too long" << endl;
      return;
   }
   strcpy(header.filename, filename.c_str());
   log << "sending header " << header << endl;
   send_packet(server, &header, sizeof header);
   recv_packet(server, &header, sizeof header);
   if(header.command != CIX_FILE) {
      log << "sent CIX_GET, server did not return CIX_FILE" << endl;
      log << "server returned " << header << endl;
      log << "get: " << strerror(header.nbytes) << endl;
   } else {
      ofstream out(filename);
      char buffer[header.nbytes+1];
      recv_packet(server, buffer, header.nbytes);
      log << "received " << header.nbytes << " bytes" << endl;
      buffer[header.nbytes] = '\0';
      out.write(buffer, header.nbytes);
      out.close(); //()
      log << "get successful" << endl;
   }
}

void cix_put (client_socket& server, string& filename) {
   cix_header header;
   header.command = CIX_PUT;
   if(filename.size() >= FILENAME_SIZE) {
      log << "put: " << filename << ": filename too long" << endl;
      return;
   }
   strcpy(header.filename, filename.c_str());
   ifstream in(filename);
   stringstream stream;
   stream << in.rdbuf();
   string bytes = stream.str();
   
   header.nbytes = bytes.size();
   log << "sending header " << header << endl;
   send_packet(server, &header, sizeof header);
   send_packet(server, bytes.c_str(), bytes.size());
   recv_packet(server, &header, sizeof header);
   cout << header.command << endl;
   log << "received header " << header << endl;
   
   if(header.command != CIX_ACK) {
      log << "sent CIX_PUT, server did not return CIX_ACK" << endl;
      log << "server returned " << header << endl;
      log << "put: " << strerror(header.nbytes) << endl;
      return;
   }
   log << "put successful" << endl;
}

void cix_rm(client_socket& server, string& filename) {
   cix_header header;
   header.command = CIX_RM;
   if(filename.size() >= FILENAME_SIZE) {
      log << "put: " << filename << ": filename too long" << endl;
      return;
   }
   strcpy(header.filename, filename.c_str());
   header.nbytes = 0;
   log << "sending header " << header << endl;
   send_packet(server, &header, sizeof header);
   recv_packet(server, &header, sizeof header);
   log << "received header " << header << endl;
   if(header.command != CIX_ACK) {
      log << "sent CIX_RM, server did not return CIX_ACK" << endl;
      log << "server returned" << header << endl;
      log << "rm: " << strerror(header.nbytes) << endl;
      return;
   }
   log << "successful rm" << endl;
}

void usage() {
   cerr << "Usage: " << log.execname() << " [host] [port]" << endl;
   throw cix_exit();
}

int main (int argc, char** argv) {
   log.execname (basename (argv[0]));
   log << "starting" << endl;
   vector<string> args (&argv[1], &argv[argc]);
   if (args.size() > 2) usage();
   string host = get_cix_server_host (args, 0);
   in_port_t port = get_cix_server_port (args, 1);
   log << to_string (hostinfo()) << endl;
   try {
      log << "connecting to " << host << " port " << port << endl;
      client_socket server (host, port);
      log << "connected to " << to_string (server) << endl;
      for (;;) {
         string line;
         getline (cin, line);
         if (cin.eof()) throw cix_exit();
         log << "command " << line << endl;
         bool one_param = true;
         string input = trim(line);
         if(input.size() == 0) continue;
         string command{}, filename{};
         size_t ind = input.find(" ");
         if(ind == string::npos) {
            command = input;
         } else {
            command = input.substr(0, ind);
            if(input.find(" ", ind + 1) == string::npos) {
               filename = input.substr(ind + 1); //was pos+1
               if(filename.find("/") != string::npos) {
                  log << "filename can not contain a slash" << endl;
                  continue;
               }
            } else {
               one_param = false;
            }
         }
         //cout << "filename: " << filename << endl;
         const auto& itor = command_map.find (command);
         cix_command cmd = itor == command_map.end()
                         ? CIX_ERROR : itor->second;
         switch (cmd) {
            case CIX_EXIT:
               if(filename.size() > 0 or !one_param) {
                  log << "exit: No additional params needed." << endl;
                  continue;
               }
               throw cix_exit();
               break;
            case CIX_HELP:
               if(filename.size() > 0 or !one_param) {
                  log << "help: No additional params needed." << endl;
                  continue;
               }
               cix_help();
               break;
            case CIX_LS:
               if(filename.size() > 0 or !one_param) {
                  log << "ls: No additional params needed." << endl;
                  continue;
               }
               cix_ls (server);
               break;
            case CIX_GET:
               if(filename.size() == 0 or !one_param) {
                  log << "usage: get filename" << endl;
                  continue;
               }
               cix_get(server, filename);
               break;
            case CIX_PUT:
               if(filename.size() == 0 or !one_param) {
                  log << "usage: put filename" << endl;
                  continue;
               }
               cix_put(server, filename);
               break;
            case CIX_RM:
               if(filename.size() == 0 or !one_param) {
                  log << "usage: rm filename" << endl;
                  continue;
               }
               cix_rm(server, filename);
               break;
            default:
               log << line << ": invalid command" << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      log << error.what() << endl;
   }catch (cix_exit& error) {
      log << "caught cix_exit" << endl;
   }
   log << "finishing" << endl;
   return 0;
}

