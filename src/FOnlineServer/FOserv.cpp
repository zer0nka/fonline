#include "stdafx.h"

#include "FOServ.h"

#include "main.h"
#include "socials.h"

//#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifndef _WIN32
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/ip.h>
#else
  #define FD_SETSIZE 1024
  #include <windows.h>
#endif

#include <string>
#include <algorithm>
#include <cmath>

#include <IniFile/IniFile.hpp>

namespace {

char* strlwr(char* str) {
  while (*str) {
    *str = ::tolower(*str);
    str++;
  }
}

char* strupr(char* str) {
  while (*str) {
    *str = ::toupper(*str);
    str++;
  }
}

int stricmp(const char* str1, const char* str2) {
  while (*str1 && *str2) {
    if (::tolower(*str1) > ::tolower(*str2)) {
      return 1;
    } else if (::tolower(*str1) > ::tolower(*str2)) {
      return -1;
    } else {
      return 0;
    }

    str1++, str2++;
  }

  if (*str1) {
    return 1;
  } else if (*str2) {
    return -1;
  } else {
    return 0;
  }
}

}  // namespace anonymous

void *zlib_alloc(void *opaque, unsigned int items, unsigned int size);
void zlib_free(void *opaque, void *address);

#define MAX_CCritterS 100

CritterID busy[MAX_CCritterS];

//!Cvet изменил концепцию ++++
//команда без префикса ~
#define CMD_SAY     0

//команды имеют префикс ~
#define CMD_EXIT    1 //выход ~exit
#define CMD_CRITID    2 //узнать ИД криттера по его имени ~id name -> crid/"false"
#define CMD_MOVECRIT  3 //двигать криттера ~move id x y -> "ok"/"false"
#define CMD_KILLCRIT  4 //убить криттера ~kill id -> "ok"/"false"
#define CMD_DISCONCRIT  5 //отсоединить криттера ~disconnect id -> "ok"/"false"
#define CMD_TOGLOBAL  6 //выход на глобал ~toglobal -> toglobal/"false"

//уровни доступа
#define ACCESS_ALL    0
#define ACCESS_MODER  1
#define ACCESS_ADMIN  2
#define ACCESS_GOD    3

struct cmdlist_def
{
  char cmd[30];
  int id;
  uint8_t access;
};

const int CMN_LIST_COUNT=12;
const cmdlist_def cmdlist[]=
{
  {"конец",CMD_EXIT,ACCESS_ALL},
  {"exit",CMD_EXIT,ACCESS_ALL},

  {"ид",CMD_CRITID,ACCESS_MODER},
  {"id",CMD_CRITID,ACCESS_MODER},

  {"двигать",CMD_MOVECRIT,ACCESS_MODER},
  {"move",CMD_MOVECRIT,ACCESS_MODER},

  {"убить",CMD_KILLCRIT,ACCESS_ADMIN},
  {"kill",CMD_KILLCRIT,ACCESS_ADMIN},

  {"отсоединить",CMD_DISCONCRIT,ACCESS_MODER},
  {"disconnect",CMD_DISCONCRIT,ACCESS_MODER},

  {"наглобал",CMD_TOGLOBAL,ACCESS_ALL},
  {"toglobal",CMD_TOGLOBAL,ACCESS_ALL},
};
//!Cvet ----

//HANDLE hDump;

CServer* CServer::self=NULL; //!Cvet

CServer::CServer()
{
  Active=0;
  outLEN=4096;
  outBUF=new char[outLEN];
  last_id=0; // Никто не присоединился
  for(int i=0;i<MAX_CCritterS;i++) busy[i]=0;

  cur_obj_id=1;

  self=this; //!Cvet
}

CServer::~CServer()
{
  Finish();
  SafeDeleteArray(outBUF);

  self=NULL; //!Cvet
}

void CServer::ClearClients() //!Cvet edit
{
  //!Cvet сохраняем данные объектов !!!!!!!!!!!!!!!!!!!dest
  SaveAllObj();

  //!Cvet сохраняем данные клиентов !!!!!!!!!!!!!!!!dest
  SaveAllDataPlayers();

  //!Cvet удаляем объекты
  for(dyn_map::iterator it2=all_obj.begin();it2!=all_obj.end();it2++)
  {
    delete (*it2).second;
  }
  all_obj.clear();

  //!Cvet удаляем клиентов
  cl_map::iterator it;
  for(it=cl.begin();it!=cl.end();it++)
  {
    if((*it).second->s != -1)
    {
      ::close((*it).second->s);
      ::deflateEnd(&(*it).second->zstrm);
    }
    delete (*it).second;
  }
  cl.clear();

  //!Cvet удаляем НПЦ !!!!!!!!!!!!!!!!!!!!
  for(it=pc.begin();it!=pc.end();it++)
  {
//    delete (*it).second;
  }
  pc.clear();

  NumClients = 0;
}

//!Cvet ++++ изменил много чего
int CServer::ConnectClient(int serv) {
  FONLINE_LOG("Попытка соеденить нового клиента...");

  sockaddr_in from;
  socklen_t addrsize = sizeof(from);
  int NewCl = ::accept(serv, (sockaddr*) &from, &addrsize);

  if (NewCl == -1) {
    FONLINE_LOG("Invalid socket #%d", NewCl);
    return 0;
  }

  CCritter* ncl = new CCritter;
  ncl->s = NewCl;
  ncl->from = from;

  ncl->zstrm.zalloc = zlib_alloc;
  ncl->zstrm.zfree = zlib_free;
  ncl->zstrm.opaque = NULL;

  if (deflateInit(&ncl->zstrm, Z_DEFAULT_COMPRESSION) != Z_OK) {
    FONLINE_LOG("DeflateInit error forSockID=%d",NewCl);
    ncl->state = STATE_DISCONNECT; //!!!!!
    return 0;
  }

  int free_place = -1;
  for(int i = 0; i < MAX_CCritterS; i++) {//проверяем есть ли свободный канал Для Игрока
    if (!busy[i]) {
      free_place = i; //опре-ся не занятый номер канала
      ncl->info.idchannel = i;
      busy[ncl->info.idchannel] = 1;
      break;
    }
  }

  if(free_place == -1) {
    FONLINE_LOG("Нет свободного канала", NewCl);
    ncl->state = STATE_DISCONNECT;
    return 0;
  }

  ncl->state = STATE_CONN;

  cl.insert(cl_map::value_type(ncl->info.idchannel, ncl));

  NumClients++; //инкремент кол-ва подключенных клиентов

  FONLINE_LOG("OK. Канал=%d. Всего клиентов в игре: %d", ncl->info.idchannel, NumClients);

  return 1;
}

void CServer::DisconnectClient(CritterID idchannel) {
  FONLINE_LOG("Disconnecting a client with the channel id =  %d...", idchannel);

  cl_map::iterator it_ds=cl.find(idchannel);
  if(it_ds==cl.end())
  {
    FONLINE_LOG("WARNING: Could not find the client.");
    return;
  }

  ::close((*it_ds).second->s);
  ::deflateEnd(&(*it_ds).second->zstrm);

  //Освобождение канала
  busy[idchannel]=0;

  if ((*it_ds).second->info.cond != COND_NOT_IN_GAME) {
    SetBits((*it_ds).second->info.flags, FCRIT_DISCONNECT);

    if((*it_ds).second->info.map) {
      SendA_Action((*it_ds).second, ACT_DISCONNECT, 0);
    } else {
      SendA_GlobalInfo((*it_ds).second->group_move, GM_INFO_CRITS);
    }

    sql.SaveDataPlayer(&(*it_ds).second->info);
  } else {
    FONLINE_LOG(".1.");
    SafeDelete((*it_ds).second); //!!!!!!!!BUG??? ВАЙ???!!!!
    FONLINE_LOG(".2.");
  }

  //Удаление клиента из списка
  cl.erase(it_ds);

  NumClients--;

  FONLINE_LOG("Отсоединение завершено. Всего клиентов в игре: %d",NumClients);
}

void CServer::RemoveCritter(CritterID id)
{
  FONLINE_LOG("Удаляем криттера id=%d",id);

  cl_map::iterator it=cr.find(id);
  if(it==cr.end()) { FONLINE_LOG("!!!WORNING!!! RemoveCritter - клиент не найден id=%d",id); return; } // Значит не нашел обьекта на карте

  if((*it).second->info.map)
  {
    //Удаляем с тайла
    EraseCrFromMap((*it).second,(*it).second->info.map,(*it).second->info.x,(*it).second->info.y);
  }

  delete (*it).second;
  cr.erase(it);

//  NumCritters--;

  FONLINE_LOG("Криттер удален");
}
//!Cvet ----

void CServer::RunGameLoop()
{
  if(!Active) return;

  TICK ticks;
  int delta;
  timeval tv;
  tv.tv_sec=0;
  tv.tv_usec=0;
  CCritter* c;

  ticks=GetMilliseconds();

  FONLINE_LOG("***   Starting Game loop   ***");

//!Cvet сбор статистики +++
  loop_time=0;
  loop_cycles=0;
  loop_min=100;
  loop_max=0;

  lt_FDsel=0;
  lt_conn=0;
  lt_input=0;
  lt_proc_cl=0;
  lt_proc_pc=0;
  lt_output=0;
  lt_discon=0;

  lt_FDsel_max=0;
  lt_conn_max=0;
  lt_input_max=0;
  lt_proc_cl_max=0;
  lt_proc_pc_max=0;
  lt_output_max=0;
  lt_discon_max=0;

  lags_count=0;

  TICK lt_ticks,lt_ticks2;
//!Cvet ---

  fd_set read_set,write_set,exc_set;

  while(!FOQuit)
  {
    ticks=GetMilliseconds()+100;

    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    FD_ZERO(&exc_set);
    FD_SET(s,&read_set);

    for(cl_map::iterator it=cl.begin();it!=cl.end();it++)
    {
      c=(*it).second;
      if(c->state!=STATE_DISCONNECT)
      {
        FD_SET(c->s,&read_set);
        FD_SET(c->s,&write_set);
        FD_SET(c->s,&exc_set);
      }
    }

    select(0,&read_set,&write_set,&exc_set,&tv);

    lt_ticks=GetMilliseconds();
    lt_FDsel+=lt_ticks-(ticks-100);

  //Новое подключение клиента
    if(FD_ISSET(s,&read_set))
    {
      ConnectClient(s);
    }

    lt_ticks2=lt_ticks;
    lt_ticks=GetMilliseconds();
    lt_conn+=lt_ticks-lt_ticks2;

  //!Cvet Прием данных от клиентов
    for(cl_map::iterator it=cl.begin();it!=cl.end();++it)
    {
      c=(*it).second;
      if((FD_ISSET(c->s,&read_set))&&(c->state!=STATE_DISCONNECT)) {
        if(!Input(c)) {
          FONLINE_LOG("Could not recieve data from a client.");
          c->state=STATE_DISCONNECT;
        }
      }
    }

    lt_ticks2=lt_ticks;
    lt_ticks=GetMilliseconds();
    lt_input+=lt_ticks-lt_ticks2;

  //Обработка данных клиентов
    for(cl_map::iterator it=cl.begin();it!=cl.end();it++)
    {
      c=(*it).second;
      if(c->state==STATE_DISCONNECT) continue;

      if(c->bin.writePosition) Process(c);
    }

    lt_ticks2=lt_ticks;
    lt_ticks=GetMilliseconds();
    lt_proc_cl+=lt_ticks-lt_ticks2;

  //Обработка НПЦ
    for(cl_map::iterator it=pc.begin();it!=pc.end();it++)
    {
      c=(*it).second;
      NPC_Process(c);
    }

    lt_ticks2=lt_ticks;
    lt_ticks=GetMilliseconds();
    lt_proc_pc+=lt_ticks-lt_ticks2;

  //Обработка Мобов
    MOBs_Proccess();

  //  lt_ticks2=lt_ticks;
  //  lt_ticks=GetMilliseconds();
  //  lt_proc_pc+=lt_ticks-lt_ticks2;

  //Посылка данных клиентов
    for(cl_map::iterator it=cl.begin();it!=cl.end();it++)
    {
      c=(*it).second;
      if(FD_ISSET(c->s,&write_set)) Output(c);
    }

    lt_ticks2=lt_ticks;
    lt_ticks=GetMilliseconds();
    lt_output+=lt_ticks-lt_ticks2;

  //Убирание отключенных клиентов
    for(cl_map::iterator it=cl.begin();it!=cl.end();)
    {
      c=(*it).second;
      it++;
      if(c->state==STATE_DISCONNECT)
      {
        DisconnectClient(c->info.idchannel);
        continue;
      }
    }

    GM_Process(ticks-GetMilliseconds());

    lt_discon+=GetMilliseconds()-lt_ticks;

  //!Cvet сбор статистики
    int32_t loop_cur=GetMilliseconds()-(ticks-100);
    loop_time+=loop_cur;
    loop_cycles++;
    if(loop_cur > loop_max) loop_max=loop_cur;
    if(loop_cur < loop_min) loop_min=loop_cur;

  //если быстро справились, то спим
    delta=ticks-GetMilliseconds();
    if(delta>0)
    {
      usleep(delta * 1000);
    }
    else lags_count++;//FONLINE_LOG("\nLag for%d ms",-delta);
  }

  //FONLINE_LOG("***   Finishing Game loop   ***\n");
}

int CServer::Input(CCritter* acl) {
  int len = recv(acl->s, inBUF, 2048, 0);
  if (len < 0 || !len) {// если клиент отвалился
    FONLINE_LOG("SOCKET_ERROR forSockID=%d", acl->s);
    return 0;
  }

  if(len==2048 || (acl->bin.writePosition+len>=acl->bin.capacity))
  {
    FONLINE_LOG("FLOOD_CONTROL forSockID=%d",acl->s);
    return 0; // если флудит игрок
  }

  acl->bin.Write(inBUF,len);

  return 1;
}

void CServer::Process(CCritter* acl) // Лист Событий
{
  MessageType msg;

  if(acl->state==STATE_CONN) //!Cvet ++++
  {
    if(acl->bin.NeedProcess())
    {
      acl->bin >> msg;

      switch(msg)
      {
      case NETMSG_LOGIN:
        Process_GetLogIn(acl);
        break;
      case NETMSG_CREATE_CLIENT:
        Process_CreateClient(acl);
        break;
      default:
        FONLINE_LOG("Неправильное MSG: %d от SockID %d при приеме LOGIN или CREATE_CCritter!",msg,acl->s);
        acl->state=STATE_DISCONNECT;
        Send_LoginMsg(acl,8);
        acl->bin.Reset(); //!Cvet при неправильном пакете данных  - удаляеться весь список
        return;
      }
    }
    acl->bin.Reset();
    return;
  } //!Cvet ----

  if(acl->state==STATE_LOGINOK) //!Cvet ++++
  {
    while(acl->bin.NeedProcess())
    {
      acl->bin >> msg;

      switch(msg)
      {
      case NETMSG_SEND_GIVE_ME_MAP:
        Send_Map(acl,acl->info.map);
        break;
      case NETMSG_SEND_LOAD_MAP_OK:
        Process_MapLoaded(acl);
        break;
      default:
        FONLINE_LOG("Неправильное MSG: %d от SockID %d при STATE_LOGINOK!",msg,acl->s);
    //    acl->state=STATE_DISCONNECT;
    //    Send_LoginMsg(acl,8);
    //    acl->bin.Reset(); //!Cvet при неправильном пакете данных  - удаляеться весь список
        continue;
      }
    }
    acl->bin.Reset();
    return;
  } //!Cvet ----

  //!Cvet если игрок мертв
  if(acl->info.cond!=COND_LIFE)
  {
    acl->bin.Reset();
    return;
  }

  while(acl->bin.NeedProcess())
  {
    acl->bin >> msg;

    switch(msg)
    {
    case NETMSG_TEXT:
      Process_GetText(acl);
      break;
    case NETMSG_DIR:
      Process_Dir(acl);
      break;
    case NETMSG_SEND_MOVE:
      Process_Move(acl);
      break;
    case NETMSG_SEND_USE_OBJECT: //!Cvet
      Process_UseObject(acl);
      break;
    case NETMSG_SEND_PICK_OBJECT: //!Cvet
      Process_PickObject(acl);
      break;
    case NETMSG_SEND_CHANGE_OBJECT: //!Cvet
      Process_ChangeObject(acl);
      break;
    case NETMSG_SEND_USE_SKILL: //!Cvet
      Process_UseSkill(acl);
      break;
    case NETMSG_SEND_TALK_NPC: //!Cvet
      Process_Talk_NPC(acl);
      break;
    case NETMSG_SEND_GET_TIME: //!Cvet
      Send_GameTime(acl);
      break;
//    case NETMSG_SEND_GIVE_ME_MAP: //!Cvet
//      Send_Map(acl,acl->info.map);
//      break;
//    case NETMSG_SEND_LOAD_MAP_OK: //!Cvet
//      Process_MapLoaded(acl);
//      break;
    case NETMSG_SEND_GIVE_GLOBAL_INFO:
      Process_GiveGlobalInfo(acl);
      break;
    case NETMSG_SEND_RULE_GLOBAL:
      Process_RuleGlobal(acl);
      break;
    default:
      FONLINE_LOG("Wrong MSG: %d from SockID %d при приеме игровых сообщений!",msg,acl->s);
      //acl->state=STATE_DISCONNECT;
      acl->bin.Reset(); //!Cvet при неправильном пакете данных  - удаляеться весь список
      return;
    }
  }
  acl->bin.Reset();
}

void CServer::Process_GetText(CCritter* acl)
{
  uint16_t len;
  char str[MAX_TEXT+1];

  acl->bin >> len;

//  if(acl->state!=STATE_GAME)
  if(acl->bin.IsError() || len>MAX_TEXT)
  {
    FONLINE_LOG("Wrong MSG data forProcess_GetText from SockID %d!",acl->s);
    acl->state=STATE_DISCONNECT;
    return;
  }

  acl->bin.Read(str,len);
  str[len]=0;

  if(acl->bin.IsError())
  {
    FONLINE_LOG("Wrong MSG data forProcess_GetText - partial recv from SockID %d!",acl->s);
    acl->state=STATE_DISCONNECT;
    return;
  }

//  FONLINE_LOG("GetText: %s",str);

  char* param;
  char* next;

  uint16_t self_len=0;
  uint16_t o_len=0;
  char self_str[MAX_TEXT+255+1]="";
  char o_str[MAX_TEXT+255+1]="";
  char mname[MAX_NAME+1];

//!Cvet переделал концепцию +++++++++++++++++++++++++
  uint16_t cmd=CMD_SAY;
  uint8_t say_param=SAY_NORM;

  if(str[0]=='~')
  {
    param=GetParam(&str[1],&next);
    if(!param)
    {
      strcpy(self_str,"Write command");
      cmd=0xFFFF;
    }
    else if(!(cmd=GetCmdNum(param,acl->info.access)))
    {
      strcpy(self_str,"wrong command or access denied");
      cmd=0xFFFF;
    }
  }
  else
    next=str;

  switch(cmd)
  {
  case CMD_SAY:
    if(next[0]=='/' || next[0]=='.') //??? next[0]=='!'
    {
      next++;
      if(!next)
      {
        strcpy(self_str, "эээ?!");
        break;
      }

      if(next[0]=='к' || next[0]=='К' || next[0]=='s' || next[0]=='S') say_param=SAY_SHOUT;
//      else if(next[0]=='о' || next[0]=='О' || next[0]=='m' || next[0]=='M') say_param=SAY_MSHOUT;
      else if(next[0]=='э' || next[0]=='Э' || next[0]=='e' || next[0]=='E') say_param=SAY_EMOTE;
      else if(next[0]=='ш' || next[0]=='Ш' || next[0]=='w' || next[0]=='W') say_param=SAY_WHISP;
      else if(next[0]=='с' || next[0]=='С' || next[0]=='$') say_param=SAY_SOCIAL;
    }

    if(say_param!=SAY_NORM)
    {
      next++;
      if(next[0]==' ') next++;
    }

    switch(say_param)
    {
    case SAY_NORM:
      if(!next)
        strcpy(self_str, "А чего сказать то?!");
      else
      {
        sprintf(self_str, "Вы: %s",next);
        sprintf(o_str, "%s: %s",MakeName(acl->info.name,mname),next);
      //  sprintf(o_str, "%s",next);
      }

      if(acl->info.map)
        SendA_Text(acl,&acl->vis_cl,self_str,o_str,say_param);
      else
        SendA_Text(acl,&acl->group_move->crit_move,self_str,o_str,say_param);
      break;
    case SAY_SHOUT:
      if(!next)
        strcpy(self_str, "Покричу, только скажи что?!");
      else
      {
        sprintf(self_str, "Вы закричали: !!!%s!!!",strupr(next));
        sprintf(o_str, "%s закричал%s: !!!%s!!!",MakeName(acl->info.name,mname),(acl->info.st[ST_GENDER]==0)?"":"а",strupr(next));
      //  sprintf(self_str, "!!!%s!!!",strupr(next));
      //  sprintf(o_str, "!!!%s!!!",next);
      }

      if(acl->info.map)
        SendA_Text(acl,&map_cr[acl->info.map],self_str,o_str,say_param);
      else
        SendA_Text(acl,&acl->group_move->crit_move,self_str,o_str,say_param);
      break;
//    case SAY_MSHOUT:
//      if(!next)
//        strcpy(self_str, "Что орем?!");
//      else
//      {
//        sprintf(self_str, "Вы заорали: !!!%s!!!",strupr(next));             //!Cvet изм. .gender=='m'
//        sprintf(o_str, "%s заорал%s: !!!%s!!!",MakeName(acl->info.name,mname),(acl->info.st[ST_GENDER]==0)?"":"а",next);
//      }
//      break;
    case SAY_EMOTE:
      if(!next)
        strcpy(self_str, "Никаких эмоций!");
      else
      {
        sprintf(self_str, "**%s %s**",MakeName(acl->info.name,mname),next);
        sprintf(o_str, "**%s %s**",mname,next);
      }

      if(acl->info.map)
        SendA_Text(acl,&acl->vis_cl,self_str,o_str,say_param);
      else
        SendA_Text(acl,&acl->group_move->crit_move,self_str,o_str,say_param);
      break;
    case SAY_WHISP: //добавил шепет
      if(!next)
        strcpy(self_str, "Че шептать будем?...");
      else
      {
        sprintf(self_str, "Вы прошептали: ...%s...",strlwr(next));              //!Cvet изм. .gender=='m'
        sprintf(o_str, "%s прошептал%s: ...%s...",MakeName(acl->info.name,mname),(acl->info.st[ST_GENDER]==0)?"":"а",strlwr(next));
      //  sprintf(self_str, "...%s...",strlwr(next));
      //  sprintf(o_str, "...%s...",next);
      }

      if(acl->info.map)
        SendA_Text(acl,&acl->vis_cl,self_str,o_str,say_param);
      else
        SendA_Text(acl,&acl->group_move->crit_move,self_str,o_str,say_param);
      break;
    case SAY_SOCIAL:
      int socid=GetSocialId(next);
      if(socid>=0)
      {
        ProcessSocial(acl,socid,next);
        return;
      }
      else
        strcpy(self_str, "Хмм?!");

      if(acl->info.map)
        SendA_Text(acl,&acl->vis_cl,self_str,o_str,say_param);
      else
        SendA_Text(acl,&acl->group_move->crit_move,self_str,o_str,say_param);
      break;
    }
    break;
  case CMD_EXIT: //выход ~exit
    FONLINE_LOG("CMD_EXIT for %s",acl->info.name);
    acl->state=STATE_DISCONNECT;
    break;
  case CMD_CRITID: //узнать ИД криттера по его имени ~id name -> crid/"false"
    //эту функцию надо перенести в клиента!
    if(next) strcpy(self_str,next);
    break;
  case CMD_MOVECRIT: //двигать криттера ~move id x y -> "ok"/"false"
    if(next) strcpy(self_str,next);
    break;
  case CMD_KILLCRIT: //убить криттера ~kill id -> "ok"/"false"
    if(next) strcpy(self_str,next);
    break;
  case CMD_DISCONCRIT: //отсоединить криттера ~disconnect id -> "ok"/"false"
    if(next) strcpy(self_str,next);
    break;
  case CMD_TOGLOBAL: //отсоединить криттера ~disconnect id -> "ok"/"false"
    if(TransitCr(acl,0,0,0,0)==TR_OK)
    {
      GM_GroupStartMove(acl);
      strcpy(self_str,"To Global - OK");
    }
    else
      strcpy(self_str,"To Global - FALSE");

    Send_Text(acl,self_str,SAY_NORM);
    break;
  case 0xFFFF:
    break;
  default:
    return;
  }
//!Cvet ------------------------------------------

//  FONLINE_LOG("self: %s\not: %s",self_str,o_str);
}

void CServer::ProcessSocial(CCritter* sender,uint16_t socid,char* aparam)
{
  char* param;
  char* next;

  uint16_t self_len=0;
  uint16_t vic_len=0;
  uint16_t all_len=0;

  char SelfStr[MAX_TEXT+255+1]="";
  char VicStr[MAX_TEXT+255+1]="";
  char AllStr[MAX_TEXT+255+1]="";

  CCritter* victim=NULL;
  param=GetParam(aparam,&next);

//  FONLINE_LOG("ProcessSocial: %s",param?param:"NULL");

  if(param && param[0] && GetPossParams(socid)!=SOC_NOPARAMS)
  {
    strlwr(param);
    if(!strcmp(param,"я") && GetPossParams(socid)!=SOC_NOSELF)
    {
      GetSocSelfStr(socid,SelfStr,AllStr,&sender->info);
    }
    else
      {
        victim=LocateByPartName(param);
        if(!victim)
          GetSocVicErrStr(socid,SelfStr,&sender->info);
        else
          GetSocVicStr(socid,SelfStr,VicStr,AllStr,&sender->info,&victim->info);
      }
  }
  else
    GetSocNoStr(socid,SelfStr,AllStr,&sender->info);

//  FONLINE_LOG("self: %s\nvic: %s\nall: %s",SelfStr,VicStr,AllStr);
  self_len=strlen(SelfStr);
  vic_len=strlen(VicStr);
  all_len=strlen(AllStr);

  MessageType msg=NETMSG_CRITTERTEXT;

  CCritter* c;
  for(cl_map::iterator it=cl.begin();it!=cl.end();it++)
  {
    c=(*it).second;

    if(c==sender && self_len)
    {
      c->bout << msg;
      c->bout << sender->info.id;
      c->bout << (uint8_t)(SAY_SOCIAL);
      c->bout << self_len;
      c->bout.Write(SelfStr,self_len);
    }
    else if(c==victim && vic_len)
    {
      c->bout << msg;
      c->bout << sender->info.id;
      c->bout << (uint8_t)(SAY_SOCIAL);
      c->bout << vic_len;
      c->bout.Write(VicStr,vic_len);
    }
    else if(all_len)
    {
      c->bout << msg;
      c->bout << sender->info.id;
      c->bout << (uint8_t)(SAY_SOCIAL);
      c->bout << all_len;
      c->bout.Write(AllStr,all_len);
    }
  }
}

CCritter* CServer::LocateByPartName(char* name)
{
  bool found=0;
  CCritter* c;
  for(cl_map::iterator it=cl.begin();it!=cl.end();it++)
  {
    c=(*it).second;

    if(PartialRight(name,c->info.name))
    {
      found=1;
      break;
    }
  }

  return found?c:NULL;
}

int CServer::Output(CCritter* acl)
{

  if(!acl->bout.writePosition) return 1;

  if(acl->bout.capacity>=outLEN)
  {
    while(acl->bout.capacity>=outLEN) outLEN<<=1;
    SafeDeleteArray(outBUF);
    outBUF=new char[outLEN];
  }

  acl->zstrm.next_in=(unsigned char*)acl->bout.data;
  acl->zstrm.avail_in=acl->bout.writePosition;
  acl->zstrm.next_out=(unsigned char*)outBUF;
  acl->zstrm.avail_out=outLEN;

  //DWORD br;
  //WriteFile(hDump,acl->bout.data,acl->bout.writePosition,&br,NULL);

  if(deflate(&acl->zstrm,Z_SYNC_FLUSH)!=Z_OK)
  {
    FONLINE_LOG("Deflate error forSockID=%d",acl->s);
    acl->state = STATE_DISCONNECT;
    return 0;
  }

  int tosend = acl->zstrm.next_out-(unsigned char*)outBUF;
  FONLINE_LOG("idchannel=%d, send %d->%d bytes",acl->info.idchannel,acl->bout.writePosition,tosend);
  int sendpos = 0;
  while(sendpos < tosend) {
    int bsent = send(acl->s, outBUF + sendpos, tosend - sendpos, 0);
    sendpos += bsent;

    if (bsent < 0) {
      FONLINE_LOG("SOCKET_ERROR whilesend forSockID=%d", acl->s);
      acl->state = STATE_DISCONNECT;
      return 0;
    }
  }

  acl->bout.Reset();

  return 1;
}

int CServer::Init() {
  if (Active) {
    return 1;
  }

  Active = 0;

  FONLINE_LOG("***   Starting initialization   ****");

  #ifdef _WIN32
    WSADATA WsaData;
    if (WSAStartup(0x0101, &WsaData)) {
      FONLINE_LOG("WSAStartup error!");
      goto SockEND;
    }
  #endif
  s = socket(AF_INET, SOCK_STREAM, 0);

  IniFile::RecordMap settings;
  if (!IniFile::LoadINI("data/server.ini", settings)) {
  return false;
  }
  //port=GetPrivateProfileInt("server","port",4000,".\\foserv.cfg");
  uint16_t port = IniFile::GetValue(settings, "server.port", 4000);

  sockaddr_in sin;
  sin.sin_family=AF_INET;
  sin.sin_port=htons(port);
  sin.sin_addr.s_addr=INADDR_ANY;

  FONLINE_LOG("Starting local server on port %d: ",port);

  if (bind(s, (sockaddr*) &sin, sizeof(sin)) < 0) {
    FONLINE_LOG("Bind error!");
    goto SockEND;
  }

  FONLINE_LOG("OK");

  if (listen(s, 5) < 0) {
    FONLINE_LOG("listen error!");
    goto SockEND;
  }

  if(!sql.Initialize())
    goto SockEND;

  LoadSocials(/*sql.mySQL*/);

//!Cvet ++++++++++++++++++++++++++++++++++++++++
  FONLINE_LOG("cr=%d",sizeof(CCritter));
  FONLINE_LOG("ci=%d",sizeof(crit_info));
  FONLINE_LOG("mi=%d",sizeof(mob_info));

  //if(!InitScriptSystem())
  //{
  //  FONLINE_LOG("Script System Init FALSE");
  //  goto SockEND;
  //}

  CreateParamsMaps();

  //шаблоны варов игроков
  if (UpdateVarsTemplate()) goto SockEND;

  //файл-менеджер
  if (!fm.Init()) goto SockEND;

  //карты
  if (!LoadAllMaps()) goto SockEND;

  //загрузка объектов
  if (!LoadAllStaticObjects()) {
    FONLINE_LOG("Загрузка статических объектов прошла со сбоями!!!");
    goto SockEND;
  }
  //создаем всех клиентов
  if (!LoadAllPlayers()) {
    FONLINE_LOG("Создание игроков прошли со сбоями!!!");
    goto SockEND;
  }
  //создаем всю динамику
  if (!LoadAllObj()) {
    FONLINE_LOG("Создание динамических объектов прошла со сбоями!!!");
    goto SockEND;
  }
  //загружаем НПЦ
  if (!NPC_LoadAll()) {
    FONLINE_LOG("Загрузка НПЦ прошла со сбоями!!!");
    goto SockEND;
  }
  //загружаем мобов
  if (!MOBs_LoadAllGroups()) {
    FONLINE_LOG("Загрузка Мобов прошла со сбоями!!!");
    goto SockEND;
  }

//  FONLINE_LOG("Создаем объекты");

//  CreateObjToPl(101,1200);
//  CreateObjToPl(102,1100);
//  CreateObjToPl(102,1200);
//  CreateObjToPl(103,1100);
//  CreateObjToPl(103,1200);
//  CreateObjToPl(104,1100);
//  CreateObjToPl(104,1200);

//  CreateObjToTile(11,61,112,1301);
//  CreateObjToTile(11,61,112,1301);
//  CreateObjToTile(11,61,112,1301);
  CreateObjToTile(11,61,112,1301);

  CreateObjToTile(11,61,113,2016);
//  CreateObjToTile(11,61,113,1301);

//  CreateObjToTile(11,58,114,1301);
  CreateObjToTile(11,64,115,1301);

  CreateObjToTile(11,58,157,7001);

//!Cvet ---------------------------------------

//  sql.PrintTableInLog("objects","*");

  Active=1;
  FONLINE_LOG("***   Initializing complete   ****\n");
  return 1;

SockEND:
  ::close(s);
  ClearClients();
  return 0;

}

void CServer::Finish()
{
  if(!Active) return;

  ::close(s);

  //FinishScriptSystem();
  NPC_ClearAll();
  ClearStaticObjects();
  ClearClients();
  UnloadSocials();
  sql.Terminate();

  FONLINE_LOG("Server stopped");

  Active=0;
}

int CServer::GetCmdNum(char* cmd, uint8_t access_level)
{
//  strlwr(cmd);

//  if(!strcmp(cmd,cmdlist[CMD_EXIT].cmd))
//    return CMD_EXIT;

  if(strlen(cmd)>=14) return 0;

  for(int cur_cmd=0;cur_cmd<CMN_LIST_COUNT;cur_cmd++)
    if(!stricmp(cmd,cmdlist[cur_cmd].cmd))
    {
      if(access_level>=cmdlist[cur_cmd].access)
        return cmdlist[cur_cmd].id;
      else
        return 0;
    }

//  for(int i=CMD_EXIT+1;cmdlist[i].cmd[0];i++)
//    if(PartialRight(cmd,(char*)cmdlist[i].cmd))
//      return i;

  return 0;
}

char* CServer::GetParam(char* cmdstr,char** next)
{
  if(!cmdstr)
  {
   *next=NULL;
   return NULL;
  }

  char* ret=NULL;
  int stop=0;
  int i;
  for(i=0;cmdstr[i];i++)
    if(cmdstr[i]!=' ') break;
  if(!cmdstr[i]) //нет первого параметра
  {
    *next=NULL;
    return ret;
  }
  ret=cmdstr+i;
  stop=i+1;
  for(i=stop;cmdstr[i];i++)
    if(cmdstr[i]==' ') break;
  if(!cmdstr[i]) //нет следующего параметра
  {
    *next=NULL;
    return ret;
  }
  cmdstr[i]=0;
  stop=i+1;
  for(i=stop;cmdstr[i];i++)
    if(cmdstr[i]!=' ') break;

  *next=cmdstr[i]?cmdstr+i:NULL;
  return ret;
}

/*char* CServer::strlwr(char* str)
{
  strlwr(str);
  for(int i=0;str[i];i++)
    if(str[i]>='А' && str[i]<='Я') str[i]+=0x20;
  return str;
}

char* CServer::strupr(char* str)
{
  strupr(str);
  for(int i=0;str[i];i++)
    if(str[i]>='а' && str[i]<='я') str[i]-=0x20;
  return str;
}*/


int CServer::PartialRight(char* str,char* et)
{
  int res=1;

  for(int i=0;str[i];i++)
    if(!et[i] || str[i]!=et[i]) return 0;

  return res;
}

char* CServer::MakeName(char* str,char* res)
{
  strcpy(res,str);
  res[0]-=0x20;
  return res;
}


void *zlib_alloc(void *opaque, unsigned int items, unsigned int size)
{
  return calloc(items, size);
}

void zlib_free(void *opaque, void *address)
{
  free(address);
}

//!Cvet ++++++++++++++++++++++++++++++++++++
int CServer::DistFast(int dx, int dy) {
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;
  if (dx < dy) return (123 * dy + 51 * dx) / 128;
  else return (123 * dx + 51 * dy) / 128;
}

int CServer::DistSqrt(int dx, int dy) {
  return (int) sqrt((double) dx * dx + (double) dy * dy);
}

void CServer::SetCheat(CCritter* acl, const char* cheat_message) {
  sql.AddCheat(acl->info.id, cheat_message);
}
