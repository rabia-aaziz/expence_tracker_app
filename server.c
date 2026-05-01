/*
 * ================================================================
 *   EXPENSE TRACKER - C HTTP BACKEND SERVER
 *   Pure C | HTTP/1.1 | JSON API | File Storage
 *
 *   API Endpoints:
 *     GET    /                    -> serve index.html
 *     GET    /api/expenses        -> list all expenses
 *     POST   /api/expenses        -> add expense
 *     DELETE /api/expenses/:id    -> delete expense
 *     GET    /api/budgets         -> list budgets
 *     POST   /api/budgets         -> set/update budget
 *     GET    /api/summary         -> stats summary
 *
 *   Compile:
 *     Linux/Mac:  gcc server.c -o server -Wall
 *     Windows:    gcc server.c -o server.exe -lws2_32
 *
 *   Run:
 *     ./server
 *     Then open: http://localhost:8080
 * ================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  #define close(s) closesocket(s)
  typedef int socklen_t;
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
#endif

/* ── Config ── */
#define PORT         8080
#define MAX_EXPENSES 1000
#define MAX_BUDGETS  20
#define DATA_FILE    "expenses.json"
#define BUDGET_FILE  "budgets.json"
#define HTML_FILE    "index.html"
#define RBUF         65536
#define SBUF         4096

/* ── Structs ── */
typedef struct {
    int   id;
    char  name[100];
    char  category[50];
    float amount;
    char  date[20];
    char  note[200];
} Expense;

typedef struct {
    char  category[50];
    float limit_amount;
} Budget;

/* ── Globals ── */
static Expense expenses[MAX_EXPENSES];
static Budget  budgets[MAX_BUDGETS];
static int     expense_count = 0;
static int     budget_count  = 0;
static int     next_id       = 1;

/* ════════════════════════════════════
   TINY JSON PARSER
════════════════════════════════════ */

/* Get string value for a key */
static int jget_str(const char *j, const char *key, char *out, int sz) {
    char k[120]; snprintf(k, sizeof(k), "\"%s\"", key);
    const char *p = strstr(j, k);
    if (!p) return 0;
    p += strlen(k);
    while (*p==' '||*p==':') p++;
    if (*p != '"') return 0;
    p++;
    int i=0;
    while (*p && *p!='"' && i<sz-1) {
        if (*p=='\\'&&*(p+1)) p++;
        out[i++] = *p++;
    }
    out[i]='\0';
    return 1;
}

/* Get float value for a key */
static float jget_float(const char *j, const char *key) {
    char k[120]; snprintf(k, sizeof(k), "\"%s\"", key);
    const char *p = strstr(j, k);
    if (!p) return 0.0f;
    p += strlen(k);
    while (*p==' '||*p==':') p++;
    return (float)atof(p);
}

/* Escape string for JSON */
static void jescape(const char *s, char *d, int dsz) {
    int j=0;
    for (int i=0; s[i]&&j<dsz-2; i++) {
        if      (s[i]=='"')  { d[j++]='\\'; d[j++]='"'; }
        else if (s[i]=='\\') { d[j++]='\\'; d[j++]='\\'; }
        else if (s[i]=='\n') { d[j++]='\\'; d[j++]='n'; }
        else if (s[i]=='\r') { d[j++]='\\'; d[j++]='r'; }
        else                 { d[j++]=s[i]; }
    }
    d[j]='\0';
}

/* ════════════════════════════════════
   JSON BUILDERS
════════════════════════════════════ */

static int expense_to_json(const Expense *e, char *buf, int sz) {
    char n[120],c[60],d[25],nt[250];
    jescape(e->name,n,sizeof(n)); jescape(e->category,c,sizeof(c));
    jescape(e->date,d,sizeof(d)); jescape(e->note,nt,sizeof(nt));
    return snprintf(buf,sz,
        "{\"id\":%d,\"name\":\"%s\",\"category\":\"%s\","
        "\"amount\":%.2f,\"date\":\"%s\",\"note\":\"%s\"}",
        e->id,n,c,e->amount,d,nt);
}

static void build_expenses_json(char *out, int sz) {
    int pos=0; pos+=snprintf(out+pos,sz-pos,"[");
    for (int i=0;i<expense_count;i++) {
        char obj[600]; expense_to_json(&expenses[i],obj,sizeof(obj));
        pos+=snprintf(out+pos,sz-pos,"%s%s",obj,i<expense_count-1?",":"");
    }
    snprintf(out+pos,sz-pos,"]");
}

static void build_budgets_json(char *out, int sz) {
    int pos=0; pos+=snprintf(out+pos,sz-pos,"[");
    for (int i=0;i<budget_count;i++) {
        char c[60]; jescape(budgets[i].category,c,sizeof(c));
        pos+=snprintf(out+pos,sz-pos,
            "{\"category\":\"%s\",\"limit\":%.2f}%s",
            c,budgets[i].limit_amount,i<budget_count-1?",":"");
    }
    snprintf(out+pos,sz-pos,"]");
}

static void build_summary_json(char *out, int sz) {
    float total=0,highest=0,lowest=-1;
    char  hname[100]="",lname[100]="";
    char  ckeys[20][50]; float ctots[20]; int cc=0;

    for (int i=0;i<expense_count;i++) {
        float a=expenses[i].amount; total+=a;
        if (a>highest){highest=a;strncpy(hname,expenses[i].name,99);}
        if (lowest<0||a<lowest){lowest=a;strncpy(lname,expenses[i].name,99);}
        int found=0;
        for (int c=0;c<cc;c++) if(!strcmp(ckeys[c],expenses[i].category)){ctots[c]+=a;found=1;break;}
        if (!found&&cc<20){strncpy(ckeys[cc],expenses[i].category,49);ctots[cc++]=a;}
    }
    if (lowest<0) lowest=0;
    float avg=expense_count>0?total/expense_count:0;

    char cjson[2048]="["; int cp=(int)strlen(cjson);
    for (int c=0;c<cc;c++){
        char ck[60]; jescape(ckeys[c],ck,sizeof(ck));
        char tmp[128];
        snprintf(tmp,sizeof(tmp),"{\"category\":\"%s\",\"total\":%.2f}%s",ck,ctots[c],c<cc-1?",":"");
        int tl=(int)strlen(tmp);
        if (cp+tl<(int)sizeof(cjson)-2){memcpy(cjson+cp,tmp,tl);cp+=tl;}
    }
    cjson[cp]=']'; cjson[cp+1]='\0';

    char hn[120],ln[120];
    jescape(hname,hn,sizeof(hn)); jescape(lname,ln,sizeof(ln));
    snprintf(out,sz,
        "{\"total\":%.2f,\"count\":%d,\"average\":%.2f,"
        "\"highest\":%.2f,\"highest_name\":\"%s\","
        "\"lowest\":%.2f,\"lowest_name\":\"%s\","
        "\"categories\":%s}",
        total,expense_count,avg,highest,hn,lowest,ln,cjson);
}

/* ════════════════════════════════════
   PERSISTENCE
════════════════════════════════════ */

static void save_expenses(void) {
    FILE *f=fopen(DATA_FILE,"w"); if(!f) return;
    fprintf(f,"{\"next_id\":%d,\"expenses\":",next_id);
    char *buf=(char*)malloc(RBUF); if(!buf){fclose(f);return;}
    build_expenses_json(buf,RBUF);
    fprintf(f,"%s}",buf); free(buf); fclose(f);
}

static void save_budgets(void) {
    FILE *f=fopen(BUDGET_FILE,"w"); if(!f) return;
    char buf[SBUF]; build_budgets_json(buf,sizeof(buf));
    fprintf(f,"%s",buf); fclose(f);
}

static void load_expenses(void) {
    FILE *f=fopen(DATA_FILE,"r"); if(!f) return;
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    if (sz<=0||sz>RBUF-1){fclose(f);return;}
    char *buf=(char*)malloc(sz+1); if(!buf){fclose(f);return;}
    fread(buf,1,sz,f); buf[sz]='\0'; fclose(f);

    float nid=jget_float(buf,"next_id");
    if (nid>0) next_id=(int)nid;

    const char *arr=strstr(buf,"\"expenses\":"); if(!arr){free(buf);return;}
    arr+=strlen("\"expenses\":"); while(*arr==' ')arr++;
    if (*arr!='['){free(buf);return;} arr++;

    expense_count=0;
    while (*arr&&*arr!=']'&&expense_count<MAX_EXPENSES) {
        while (*arr&&*arr!='{'&&*arr!=']') arr++;
        if (*arr!='{') break;
        int depth=1; const char *st=arr; arr++;
        while (*arr&&depth>0){if(*arr=='{')depth++;else if(*arr=='}')depth--;arr++;}
        int olen=(int)(arr-st);
        char *obj=(char*)malloc(olen+1); if(!obj) break;
        strncpy(obj,st,olen); obj[olen]='\0';
        Expense *e=&expenses[expense_count]; memset(e,0,sizeof(Expense));
        e->id=(int)jget_float(obj,"id"); e->amount=jget_float(obj,"amount");
        jget_str(obj,"name",e->name,sizeof(e->name));
        jget_str(obj,"category",e->category,sizeof(e->category));
        jget_str(obj,"date",e->date,sizeof(e->date));
        jget_str(obj,"note",e->note,sizeof(e->note));
        if (e->id>0) expense_count++;
        free(obj);
        while (*arr==','||*arr==' ') arr++;
    }
    free(buf);
    printf("[INFO] Loaded %d expenses (next_id=%d)\n",expense_count,next_id);
}

static void load_budgets(void) {
    FILE *f=fopen(BUDGET_FILE,"r"); if(!f) return;
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    if (sz<=0||sz>SBUF-1){fclose(f);return;}
    char *buf=(char*)malloc(sz+1); if(!buf){fclose(f);return;}
    fread(buf,1,sz,f); buf[sz]='\0'; fclose(f);
    budget_count=0;
    const char *p=buf; while(*p&&*p!='[')p++; if(*p=='[')p++;
    while (*p&&*p!=']'&&budget_count<MAX_BUDGETS) {
        while (*p&&*p!='{'&&*p!=']') p++;
        if (*p!='{') break;
        int depth=1; const char *st=p; p++;
        while (*p&&depth>0){if(*p=='{')depth++;else if(*p=='}')depth--;p++;}
        int len=(int)(p-st);
        char *obj=(char*)malloc(len+1); if(!obj) break;
        strncpy(obj,st,len); obj[len]='\0';
        Budget *b=&budgets[budget_count];
        jget_str(obj,"category",b->category,sizeof(b->category));
        b->limit_amount=jget_float(obj,"limit");
        if (strlen(b->category)>0) budget_count++;
        free(obj);
        while (*p==','||*p==' ') p++;
    }
    free(buf);
    printf("[INFO] Loaded %d budgets\n",budget_count);
}

/* ════════════════════════════════════
   HTTP HELPERS
════════════════════════════════════ */

static void send_resp(int sk, int status, const char *ct, const char *body, int blen) {
    const char *st="OK";
    if (status==201) st="Created";
    else if (status==400) st="Bad Request";
    else if (status==404) st="Not Found";
    else if (status==405) st="Method Not Allowed";
    else if (status==500) st="Internal Server Error";
    char hdr[512];
    int hl=snprintf(hdr,sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET,POST,DELETE,OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n\r\n",
        status,st,ct,blen);
    send(sk,hdr,hl,0);
    if (body&&blen>0) send(sk,body,blen,0);
}

static void send_json(int sk, int status, const char *json) {
    send_resp(sk,status,"application/json",json,(int)strlen(json));
}

static void get_method(const char *req, char *m, int sz) {
    int i=0;
    while (req[i] && req[i]!=' ' && i<sz-1) { m[i]=req[i]; i++; }
    m[i]='\0';
}
static void get_path(const char *req, char *p, int sz) {
    const char *r=req; while(*r&&*r!=' ')r++; if(*r)r++;
    int i=0; while(*r&&*r!=' '&&*r!='\r'&&i<sz-1) p[i++]=*r++; p[i]='\0';
}
static void get_body(const char *req, char *body, int sz) {
    const char *b=strstr(req,"\r\n\r\n");
    if (!b){body[0]='\0';return;} b+=4;
    strncpy(body,b,sz-1); body[sz-1]='\0';
}

/* ════════════════════════════════════
   ROUTE HANDLERS
════════════════════════════════════ */

static void route_get_expenses(int sk) {
    char *buf=(char*)malloc(RBUF); if(!buf){send_json(sk,500,"{\"error\":\"oom\"}");return;}
    build_expenses_json(buf,RBUF);
    send_json(sk,200,buf); free(buf);
}

static void route_post_expense(int sk, const char *body) {
    if (expense_count>=MAX_EXPENSES){send_json(sk,400,"{\"error\":\"full\"}");return;}
    Expense *e=&expenses[expense_count]; memset(e,0,sizeof(Expense));
    e->id=next_id++; e->amount=jget_float(body,"amount");
    jget_str(body,"name",e->name,sizeof(e->name));
    jget_str(body,"category",e->category,sizeof(e->category));
    jget_str(body,"date",e->date,sizeof(e->date));
    jget_str(body,"note",e->note,sizeof(e->note));
    if (!strlen(e->name)||e->amount<=0){send_json(sk,400,"{\"error\":\"name+amount required\"}");return;}
    if (!strlen(e->category)) strcpy(e->category,"Other");
    if (!strlen(e->date)){
        time_t t=time(NULL); struct tm *tm=localtime(&t);
        strftime(e->date,sizeof(e->date),"%Y-%m-%d",tm);
    }
    expense_count++; save_expenses();
    char resp[600]; expense_to_json(e,resp,sizeof(resp));
    send_json(sk,201,resp);
    printf("[ADD]  id=%d  %.2f PKR  %s  [%s]\n",e->id,e->amount,e->name,e->category);
}

static void route_delete_expense(int sk, const char *path) {
    int del_id=atoi(path+strlen("/api/expenses/"));
    if (del_id<=0){send_json(sk,400,"{\"error\":\"bad id\"}");return;}
    for (int i=0;i<expense_count;i++){
        if (expenses[i].id==del_id){
            for (int j=i;j<expense_count-1;j++) expenses[j]=expenses[j+1];
            expense_count--; save_expenses();
            printf("[DEL]  id=%d\n",del_id);
            char r[64]; snprintf(r,sizeof(r),"{\"deleted\":%d}",del_id);
            send_json(sk,200,r); return;
        }
    }
    send_json(sk,404,"{\"error\":\"not found\"}");
}

static void route_get_budgets(int sk) {
    char buf[SBUF]; build_budgets_json(buf,sizeof(buf)); send_json(sk,200,buf);
}

static void route_post_budget(int sk, const char *body) {
    char cat[50]; float lim=jget_float(body,"limit");
    jget_str(body,"category",cat,sizeof(cat));
    if (!strlen(cat)||lim<=0){send_json(sk,400,"{\"error\":\"category+limit required\"}");return;}
    for (int i=0;i<budget_count;i++){
        if (!strcmp(budgets[i].category,cat)){
            budgets[i].limit_amount=lim; save_budgets();
            send_json(sk,200,"{\"updated\":true}"); return;
        }
    }
    if (budget_count<MAX_BUDGETS){
        strncpy(budgets[budget_count].category,cat,49);
        budgets[budget_count].limit_amount=lim;
        budget_count++; save_budgets();
        send_json(sk,201,"{\"created\":true}");
    } else send_json(sk,400,"{\"error\":\"budget limit reached\"}");
}

static void route_get_summary(int sk) {
    char buf[SBUF*2]; build_summary_json(buf,sizeof(buf)); send_json(sk,200,buf);
}

static void route_html(int sk) {
    FILE *f=fopen(HTML_FILE,"r");
    if (!f){
        const char *msg="<h2>index.html not found</h2>"
                        "<p>Place index.html in the same folder as the server.</p>";
        send_resp(sk,404,"text/html",msg,(int)strlen(msg)); return;
    }
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    char *html=(char*)malloc(sz+1); if(!html){fclose(f);send_json(sk,500,"{}");return;}
    fread(html,1,sz,f); html[sz]='\0'; fclose(f);
    send_resp(sk,200,"text/html; charset=utf-8",html,(int)sz);
    free(html);
}

/* ════════════════════════════════════
   ROUTER
════════════════════════════════════ */

static void handle_request(int csk, const char *req) {
    char method[12], path[256];
    get_method(req,method,sizeof(method));
    get_path(req,path,sizeof(path));
    printf("[%s] %s\n",method,path);

    if (!strcmp(method,"OPTIONS")){send_json(csk,200,"{}");return;}

    if (!strcmp(path,"/")||!strcmp(path,"/index.html")) {
        route_html(csk);
    } else if (!strcmp(path,"/api/expenses")) {
        if      (!strcmp(method,"GET"))  route_get_expenses(csk);
        else if (!strcmp(method,"POST")){char b[SBUF];get_body(req,b,sizeof(b));route_post_expense(csk,b);}
        else send_json(csk,405,"{\"error\":\"method not allowed\"}");
    } else if (!strncmp(path,"/api/expenses/",14)) {
        if (!strcmp(method,"DELETE")) route_delete_expense(csk,path);
        else send_json(csk,405,"{\"error\":\"method not allowed\"}");
    } else if (!strcmp(path,"/api/budgets")) {
        if      (!strcmp(method,"GET"))  route_get_budgets(csk);
        else if (!strcmp(method,"POST")){char b[SBUF];get_body(req,b,sizeof(b));route_post_budget(csk,b);}
        else send_json(csk,405,"{\"error\":\"method not allowed\"}");
    } else if (!strcmp(path,"/api/summary")) {
        if (!strcmp(method,"GET")) route_get_summary(csk);
        else send_json(csk,405,"{\"error\":\"method not allowed\"}");
    } else {
        send_json(csk,404,"{\"error\":\"not found\"}");
    }
}

/* ════════════════════════════════════
   MAIN
════════════════════════════════════ */

int main(void) {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2),&wsa);
#endif
    load_expenses(); load_budgets();

    int sfd=socket(AF_INET,SOCK_STREAM,0);
    if (sfd<0){perror("socket");return 1;}
    int opt=1; setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,(char*)&opt,sizeof(opt));

    struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET; addr.sin_addr.s_addr=INADDR_ANY; addr.sin_port=htons(PORT);
    if (bind(sfd,(struct sockaddr*)&addr,sizeof(addr))<0){perror("bind");return 1;}
    if (listen(sfd,10)<0){perror("listen");return 1;}

    printf("\n");
    printf("  ╔══════════════════════════════════════╗\n");
    printf("  ║   💰  EXPENSE TRACKER  C SERVER       ║\n");
    printf("  ║   http://localhost:%d               ║\n",PORT);
    printf("  ║   Open that URL in your browser       ║\n");
    printf("  ║   Ctrl+C  to stop                     ║\n");
    printf("  ╚══════════════════════════════════════╝\n\n");

    while (1) {
        struct sockaddr_in ca; socklen_t cl=sizeof(ca);
        int cfd=accept(sfd,(struct sockaddr*)&ca,&cl);
        if (cfd<0){perror("accept");continue;}
        char *req=(char*)malloc(RBUF); if(!req){close(cfd);continue;}
        memset(req,0,RBUF);
        int n=recv(cfd,req,RBUF-1,0);
        if (n>0){req[n]='\0'; handle_request(cfd,req);}
        free(req); close(cfd);
    }
    close(sfd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
