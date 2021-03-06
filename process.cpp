/*
 *  process.cpp
 *  PlanetWars
 *
 *  Created by Albert Zeyer on 15.09.10.
 *  code under GPLv3
 *
 */

#ifndef _WIN32

#include <iostream>
#include <vector>
#include <string>
#include <stdio.h> // strerror etc
#include <unistd.h> // pipe, fork, etc
#include <errno.h>
#include <signal.h> // kill etc
#include <fcntl.h>
#include <sys/wait.h>
#include <string.h>
#include "process.h"
#include "utils.h"

void Process::run() {
	using namespace std;
	
	int pipe_mainToFork[2];	// 0: read from, 1: write to
	if(pipe(pipe_mainToFork) != 0) { // error creating pipe
		cerr << "Process::run(): cannot create first pipe: " << strerror(errno) << endl;		
		return;
	}
	
	int pipe_forkToMain[2];	// 0: read from, 1: write to
	if(pipe(pipe_forkToMain) != 0) { // error creating pipe
		cerr << "Process::run(): cannot create second pipe: " << strerror(errno) << endl;		
		return;
	}	
	
	pid_t p = fork();
	if(p < 0) { // error forking
		cerr << "Process::run(): cannot fork: " << strerror(errno) << endl;		
		return;
	}
	else if(p == 0) { // fork		
		// close other ends of pipes
		close(pipe_mainToFork[1]);
		close(pipe_forkToMain[0]);
		
		// redirect stdin/stdout to our pipes
		dup2(pipe_mainToFork[0], STDIN_FILENO);
		dup2(pipe_forkToMain[1], STDOUT_FILENO);
		
		// run
		std::vector<std::string> paramsS = Tokenize(cmd, " ");
		char** params = new char*[paramsS.size() + 1];
		for(size_t i = 0; i < paramsS.size(); ++i)
			params[i] = (char*)paramsS[i].c_str();
		params[paramsS.size()] = NULL;
		execvp(params[0], params);
		cerr << "ERROR: cannot run " << params[0] << ": " << strerror(errno) << endl;
		_exit(0);
	}
	else { // parent
		   // close other ends
		close(pipe_mainToFork[0]);
		close(pipe_forkToMain[1]);
		
		forkId = p;
		running = true;
		forkInputFd = pipe_mainToFork[1];
		forkOutputFd = pipe_forkToMain[0];
		
		// we don't want blocking on the fork output reading
		fcntl(forkOutputFd, F_SETFL, O_NONBLOCK);
	}
}

void Process::destroy() {
	if(running) {
		kill(forkId, SIGTERM);
	}
}

void Process::waitForExit() {
	if(running) {
		waitpid(forkId, NULL, 0);
		close(forkInputFd);
		close(forkOutputFd);
		*this = Process(); // reset
	}	
}

static timeval millisecsToTimeval(size_t ms) {
	timeval v;
	v.tv_sec = ms / 1000;
	v.tv_usec = (ms % 1000) * 1000;
	return v;
}

bool Process::readLine(std::string& s, size_t timeout) {
	size_t startTime = (size_t)currentTimeMillis();
	
	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(forkOutputFd, &fdset);
	
	while(true) {
		char c;
		while(read(forkOutputFd, &c, 1) > 0) {
			if(c == '\n') {
				s = outbuffer;
				outbuffer = "";
				return true;
			}
			outbuffer += c;
		}
		
		size_t dt = currentTimeMillis() - startTime;
		if((size_t)dt > timeout) return false;
		timeval t = millisecsToTimeval(timeout - dt);
		
		if(select(FD_SETSIZE, &fdset, NULL, NULL, &t) <= 0) // timeout or error
			return false;		
	}
	return false;
}

void Process::flush() {
	size_t n = 0;
	while(n < inbuffer.size())
		n += write(forkInputFd, inbuffer.c_str() + n, inbuffer.size() - n);
	inbuffer = "";
}

#endif // not _WIN32
