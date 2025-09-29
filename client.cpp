/*
	Original author of the starter code
    Tanzir Ahmed
    Department of Computer Science & Engineering
    Texas A&M University
    Date: 2/8/20
	
	Please include your Name, UIN, and the date below
	Name:
	UIN:
	Date:
*/
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"
#include "FIFORequestChannel.h"
#include <algorithm>

using namespace std;

static int g_buffer_capacity = MAX_MESSAGE;


static void recieved_dir(){
    struct stat st;
    if(stat("received", &st) == -1){
        mkdir("received", 0700);
    }
}


static void request_and_print_datapoint(FIFORequestChannel& chan, int p, double t, int e){
    datamsg req(p, t, e);
    double val = 0.0;
    chan.cwrite(&req, sizeof(req));
    chan.cread(&val, sizeof(val));
    cout << "For person " << p << ", at time " << t << ", the value of ecg " << e << " is " << val << endl;
}

static void dump_first_1000(FIFORequestChannel& chan, int person){
    recieved_dir();
    FILE* fp = fopen("received/x1.csv","wb");
    if(!fp){
        EXITONERROR("fopen received/x1.csv failed");
    }

    for(int i = 0; i < 1000; ++i){
        double t = i * 0.004;
        datamsg r1(person, t, 1); double e1 = 0.0;
        chan.cwrite(&r1, sizeof(r1)); chan.cread(&e1, sizeof(e1));
        datamsg r2(person, t, 2); double e2 = 0.0;
        chan.cwrite(&r2, sizeof(r2)); chan.cread(&e2, sizeof(e2));
        fprintf(fp, "%.3f, %.10lf, %.10lf\n", t, e1, e2);
    }

    fclose(fp);
}


 static __int64_t request_file_size(FIFORequestChannel& chan, const string& fname){
     filemsg fm(0,0);
    int len = sizeof(filemsg) + fname.size() + 1;
    vector<char> req(len);
    memcpy(req.data(), &fm, sizeof(filemsg));
    strcpy(req.data() + sizeof(filemsg), fname.c_str());
    chan.cwrite(req.data(), len);
    __int64_t fsize = 0;
    chan.cread(&fsize, sizeof(fsize));
    return fsize;
}

static void fetch_file(FIFORequestChannel& chan, const string& fname){
    recieved_dir();
    __int64_t fsize = request_file_size(chan , fname);
    string outpath = string("received/") +fname;
    FILE* out = fopen(outpath.c_str(), "wb+");
    __int64_t offset =0;
    while (offset < fsize){
        int chunk = min<__int64_t>(fsize - offset, g_buffer_capacity);
        int plen = sizeof(filemsg) +fname.size() + 1;
        vector<char> req(plen);
        filemsg fm(offset,chunk);
        memcpy(req.data(), &fm,sizeof(filemsg));
        strcpy(req.data() + sizeof(filemsg),fname.c_str());
        chan.cwrite(req.data(),plen);
        vector<char> data(chunk);
        int n = chan.cread(data.data(),chunk);
        fseeko(out, offset, SEEK_SET);
        fwrite(data.data(),1,n, out);
        offset += n;
    }
    fclose(out);
}



int main (int argc, char *argv[]){	

    int opt,p=0,e=1,m_from_cli=-1;
    double t= 0.0;
    string filename="";
    bool want_new_channel=false;

    while ((opt = getopt(argc, argv, "p:t:e:f:m:c")) != -1){
        switch(opt){
            case 'p': p = atoi(optarg);
			break;
            case 't': t = atof(optarg);
			break;
            case 'e': e = atoi(optarg);
			break;
            case 'f': filename = optarg;
			break;
            case 'm': g_buffer_capacity = atoi(optarg); m_from_cli = g_buffer_capacity;
			break;
            case 'c': want_new_channel = true;
			break;
        }
    }

    pid_t pid = fork();
    if(pid==0){
        if(m_from_cli>0){
            char mstr[32]; sprintf(mstr,"%d",m_from_cli);
            char* const args[]={(char*)"./server",(char*)"-m", mstr,nullptr};
            execvp(args[0],args);
        } 
		 else{
            char* const args[]={(char*)"./server",nullptr};
            execvp(args[0],args);
        }

        EXITONERROR("execvp failed");
    }

    usleep(50000);

    FIFORequestChannel control("control", FIFORequestChannel::CLIENT_SIDE);
    FIFORequestChannel* active=&control;

    FIFORequestChannel* newchan=nullptr;
    if(want_new_channel){
        MESSAGE_TYPE m = NEWCHANNEL_MSG;
        control.cwrite(&m,sizeof(m));
        char namebuf[64]; memset(namebuf,0,sizeof(namebuf));
        control.cread(namebuf,sizeof(namebuf));
        newchan=new FIFORequestChannel(namebuf,FIFORequestChannel::CLIENT_SIDE);
        active=newchan;
    }

    if(p!=0 && !filename.size() && (t!=0.0 || e!=1)){
        request_and_print_datapoint(*active,p,t,e);
    }

    if(p!=0 && filename.empty() && (t==0.0 && e==1)){
        dump_first_1000(*active,p);
    }

    if(!filename.empty()){
        fetch_file(*active,filename);
    }

    MESSAGE_TYPE quit = QUIT_MSG;
    active->cwrite(&quit,sizeof(quit));

    if(newchan){
        control.cwrite(&quit,sizeof(quit));
        delete newchan;
    }

    int status=0; 
	waitpid(pid,&status,0);
    return 0;
}
