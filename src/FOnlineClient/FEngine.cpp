#include "FEngine.h"
#include "common.h"
#include "version.h"
#include "keyb.h"

//#include <SimpleLeakDetector/SimpleLeakDetector.hpp>

template<class T> void SetBit(T& bits, unsigned int bit, bool value) {
  T mask = T(1) << bit;
  bits = (bits & (~mask)) | (T(value ? 1 : 0) << bit);
}
template<class T> bool GetBit(T& bits, unsigned int bit) {
  return ((bits >> bit) & 0x01) != 0;
}

void *zlib_alloc(void *opaque, unsigned int items, unsigned int size) {
  return calloc(items, size);
}

void zlib_free(void *opaque, void *address) {
  free(address);
}

FOnlineEngine::FOnlineEngine(): initialized(0),hWnd(NULL),lpD3D(NULL),lpDevice(NULL),islost(0),lpDInput(NULL),lpKeyboard(NULL),lpMouse(NULL)
{
  dilost=0;

  cmn_di_left=0;
  cmn_di_right=0;
  cmn_di_up=0;
  cmn_di_down=0;

  cmn_di_mleft=0;
  cmn_di_mright=0;
  cmn_di_mup=0;
  cmn_di_mdown=0;

  CtrlDwn=0;
  AltDwn=0;
  ShiftDwn=0;
  edit_mode=0;
//!Cvet +++++++++++++++++++++++
  SetScreen(SCREEN_LOGIN);
  SetCur(CUR_DEFAULT);
//!Cvet -----------------------

  ed_str[0]=0;
  cur_edit=0; //!Cvet
  lang=LANG_RUS;

  comlen=4096;
  compos=0;
  ComBuf=new char[comlen];
  zstrmok=0;
  state=0;
  stat_com=0;
  stat_decom=0;

  Game_FPS=0;
}

int FOnlineEngine::Init(HWND _hWnd)
{
  FONLINE_LOG("Initializing.\n");

  HRESULT hr;

  hWnd=_hWnd;

  InitKeyboard();

  FONLINE_LOG("Creating Direct3D.");
  lpD3D = Direct3DCreate8(D3D_SDK_VERSION);
  if (!lpD3D){
    ReportErrorMessage("Engine Init", "Could not create Direct3D.\nPleas make sure you have DirectX version 8.1 or higher installed.");
    return 0;
  }

  FONLINE_LOG("Setting display mode.");
  D3DDISPLAYMODE d3ddm;
  hr = lpD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT,&d3ddm);
  if (hr != D3D_OK){
    ReportErrorMessage("GetAdapterDisplayMode",(char*)DXGetErrorString8(hr));
    return 0;
  }

  FONLINE_LOG("Creating Direct3D device.");

  D3DPRESENT_PARAMETERS d3dpp;
  ZeroMemory(&d3dpp,sizeof(d3dpp));
  d3dpp.Windowed = opt_fullscr ? 0 : 1;
  d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  //d3dpp.Flags=D3DPRESENTFLAG_LOCKABLE_BACKBUFFER; //??? lockable need
  if(!opt_fullscr) {
    d3dpp.BackBufferFormat=d3ddm.Format;
  } else {
    d3dpp.BackBufferWidth = screen_width[opt_screen_mode];
    d3dpp.BackBufferHeight = screen_height[opt_screen_mode];
    d3dpp.BackBufferFormat = D3DFMT_A8R8G8B8;

    if (!opt_vsync) {
      d3dpp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    }
  }

  hr = lpD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
               &d3dpp, &lpDevice);
  if (hr != D3D_OK){
    ReportErrorMessage("CreateDevice",(char*)DXGetErrorString8(hr));
    return 0;
  }

  FONLINE_LOG("Setting render mode.");

  // Disable lighting.
  lpDevice->SetRenderState(D3DRS_LIGHTING,FALSE);
  // Disable Z-Buffer.
  lpDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
  // Disable culling.
  lpDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

  // Enable alpha blending.
  lpDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  lpDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  lpDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

  // Enable alpha testing.
  lpDevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
  lpDevice->SetRenderState(D3DRS_ALPHAREF, 0x08);
  lpDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);

  // Enable linear filtration.
  lpDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
  lpDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
  lpDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
  lpDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);

  lpDevice->SetVertexShader(D3DFVF_VERTEX_FORMAT);

  // Clear the back buffer.
  lpDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0,0,0), 1.0, 0);

  if (!InitDirectInput()) return 0;

  if (!spriteManager.Init(lpDevice)) return 0;

  if (!soundManager.Init()) return 0;

  FONLINE_LOG("Loading splash...");

  char* name_splash=new char[64];
  GetRandomSplash(name_splash);
  if (!(splash = spriteManager.LoadRix(name_splash, PT_ART_SPLASH))) {
    if (name_splash != NULL) {
      delete [] name_splash;
      name_splash = NULL;
    }

    return 0;
  }

  if (name_splash != NULL) {
    delete [] name_splash;
    name_splash = NULL;
  }

  spriteManager.NextSurface();
  lpDevice->BeginScene();
  spriteManager.DrawSprite(splash,0,0,COLOR_DEFAULT);
  spriteManager.Flush();
  lpDevice->EndScene();
  lpDevice->Present(NULL, NULL, NULL, NULL);

  FONLINE_LOG("OK\n");

  if(!fnt.Init(lpDevice,spriteManager.GetVB(),spriteManager.GetIB())) return 0;

//!Cvet++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CreateParamsMaps();

  if(!Init_Iface())
  {
    FONLINE_LOG("FALSE\n");
    return 0;
  }

  SetChosenAction(ACTION_NONE);

  //загружаем статические объекты
  FONLINE_LOG("Loading static objects.");

  FILE *o_cf;
  FILE *o_cf2;
  params_map::iterator it_o;

  for(stat_map::iterator it = all_s_obj.begin(); it != all_s_obj.end(); it++) {
    if (it->second != NULL) {
      delete it->second;
      it->second = NULL;
    }
  }
  all_s_obj.clear();

  if ((o_cf = fopen("data\\objects\\all_obj.st","rt")) == NULL) {
    FONLINE_LOG("Файл all_obj.st не найден\n");
    return 0;
  }

  int cnt_obj=0;
  int tmpi=0;
  char tmpc[256];

  while(!feof(o_cf))
  {
    tmpi=0;
    fscanf(o_cf,"%d",&tmpi);
    if(!tmpi) break;

    sprintf(tmpc,".\\data\\objects\\%d.st",tmpi);
    if((o_cf2=fopen(tmpc,"rt"))==NULL)
    {
      FONLINE_LOG("Файл |%s| не найден\n",tmpc);
      return 0;
    }

    stat_obj* new_obj;
    new_obj= new stat_obj;

    new_obj->id=tmpi;
    fscanf(o_cf2,"%s",&tmpc);
    it_o=object_map.find(tmpc);
    if(it_o==object_map.end())
    {
      FONLINE_LOG("Параметр |%s| не найден",tmpc);
      return 0;
    }
    new_obj->type=(*it_o).second;

    while(!feof(o_cf2))
    {
      fscanf(o_cf2,"%s%d",&tmpc,&tmpi);

      it_o=object_map.find(tmpc);
      if(it_o==object_map.end())
      {
        FONLINE_LOG("Параметр |%s| не найден",tmpc);
        return 0;
      }
      new_obj->p[(*it_o).second]=tmpi;
    }

    all_s_obj[new_obj->id]=new_obj;

    fclose(o_cf2);
    cnt_obj++;
  }
  fclose(o_cf);

  //имена/инфа объектов
  char get_name_obj[MAX_OBJECT_NAME];
  char get_info_obj[MAX_OBJECT_INFO];
  char key_obj[64];
  for(stat_map::iterator it_so=all_s_obj.begin();it_so!=all_s_obj.end();it_so++)
  {
    sprintf(key_obj,"%d",(*it_so).second->id);
    GetPrivateProfileString("info",key_obj,"error",&get_name_obj[0],MAX_OBJECT_NAME,".\\data\\objects\\info_objects.txt");

    sprintf(key_obj,"*%d",(*it_so).second->id);
    GetPrivateProfileString("info",key_obj,"error",&get_info_obj[0],MAX_OBJECT_INFO,".\\data\\objects\\info_objects.txt");

    name_obj.insert(string_map::value_type((*it_so).second->id,std::string(get_name_obj)));
    info_obj.insert(string_map::value_type((*it_so).second->id,std::string(get_info_obj)));
  }

  FONLINE_LOG("OK (%d объектов)\n",cnt_obj);
//!Cvet ------------------------------------------------------------------------------------

  if(!hexField.Init(&spriteManager)) return 0;

  state=STATE_DISCONNECT; //!Cvet

  SetColor(COLOR_DEFAULT);

  if(!opt_fullscr)
  {
    RECT r;
    GetWindowRect(hWnd,&r);
    SetCursorPos(r.left+2+320,r.top+22+240);
  }
  else
  {
    SetCursorPos(320,240);
  }
  ShowCursor(0);
  cur_x=320;cur_y=240;

  FONLINE_LOG("Всего спрайтов загружено: %d\n",spriteManager.GetLoadedCnt());

  FONLINE_LOG("FEngine Initialization complete\n");
  initialized=1;

  return 1;
}

int FOnlineEngine::InitDirectInput()
{
  FONLINE_LOG("DInput init...\n");
  HRESULT hr = DirectInput8Create(GetModuleHandle(NULL),DIRECTINPUT_VERSION,IID_IDirectInput8,(void**)&lpDInput,NULL);
  if(hr!=DI_OK)
  {
    ReportErrorMessage("FOnlineEngine InitDirectInput","Не могу создать DirectInput");
    return 0;
  }

  // Obtain an interface to the system keyboard device.
  hr = lpDInput->CreateDevice(GUID_SysKeyboard,&lpKeyboard,NULL);
  if(hr!=DI_OK)
  {
    ReportErrorMessage("FOnlineEngine InitDirectInput","Не могу создать GUID_SysKeyboard");
    return 0;
  }

  hr = lpDInput->CreateDevice(GUID_SysMouse,&lpMouse,NULL);
  if(hr!=DI_OK)
  {
    ReportErrorMessage("FOnlineEngine InitDirectInput","Не могу создать GUID_SysMouse");
    return 0;
  }
  // Set the data format to "keyboard format" - a predefined data format
  // This tells DirectInput that we will be passing an array
  // of 256 bytes to IDirectInputDevice::GetDeviceState.
  hr = lpKeyboard->SetDataFormat(&c_dfDIKeyboard);
  if(hr!=DI_OK)
  {
    ReportErrorMessage("FOnlineEngine InitDirectInput","Не могу установить формат данных для клавиатуры");
    return 0;
  }

  hr = lpMouse->SetDataFormat(&c_dfDIMouse2);
  if(hr!=DI_OK)
  {
    ReportErrorMessage("FOnlineEngine InitDirectInput","Не могу установить формат данных для мышки");
    return 0;
  }


  hr = lpKeyboard->SetCooperativeLevel( hWnd,DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
  if(hr!=DI_OK)
  {
    ReportErrorMessage("FOnlineEngine InitDirectInput","Ошибка SetCooperativeLevel для клавиатуры");
    return 0;
  }

//  hr = lpMouse->SetCooperativeLevel( hWnd,DISCL_FOREGROUND | (opt_fullscr?DISCL_EXCLUSIVE:DISCL_EXCLUSIVE));
  hr = lpMouse->SetCooperativeLevel( hWnd,DISCL_FOREGROUND | DISCL_EXCLUSIVE);//!Cvet сделал эксклюзив для всего
  if(hr!=DI_OK)
  {
    ReportErrorMessage("FOnlineEngine InitDirectInput","Ошибка SetCooperativeLevel для мышки");
    return 0;
  }

  // IMPORTANT STEP TO USE BUFFERED DEVICE DATA!
  DIPROPDWORD dipdw;

  dipdw.diph.dwSize       = sizeof(DIPROPDWORD);
  dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
  dipdw.diph.dwObj        = 0;
  dipdw.diph.dwHow        = DIPH_DEVICE;
  dipdw.dwData            = DI_BUF_SIZE; // Arbitary buffer size

  hr = lpKeyboard->SetProperty(DIPROP_BUFFERSIZE,&dipdw.diph);
  if(hr!=DI_OK)
  {
    ReportErrorMessage("FOnlineEngine InitDirectInput","Ошибка настройки буфера приема для клавиатуры");
    return 0;
  }

  hr = lpMouse->SetProperty(DIPROP_BUFFERSIZE,&dipdw.diph);
  if(hr!=DI_OK)
  {
    ReportErrorMessage("FOnlineEngine InitDirectInput","Ошибка настройки буфера приема для мышки");
    return 0;
  }

  // Acquire the newly created device
  lpKeyboard->Acquire();
  lpMouse->Acquire();

  FONLINE_LOG("DInput init OK\n");

  return 1;
}

void FOnlineEngine::Clear()
{
  FONLINE_LOG("\nFEngine Clear...\n");

  ClearKeyb();

  NetDiscon();

  hexField.Clear();
  spriteManager.Clear();
  fnt.Clear();
  soundManager.Clear();

  for (stat_map::iterator i = all_s_obj.begin(); i != all_s_obj.end(); ++i) {
    assert(i->second != NULL);
    delete i->second;
  }
  all_s_obj.clear();

  if (lpDevice != NULL) {
    lpDevice->Release();
    lpDevice = NULL;
  }

  if (lpD3D != NULL) {
    lpD3D->Release();
    lpD3D = NULL;
  }

  if (lpKeyboard != NULL) {
    lpKeyboard->Unacquire();
    lpKeyboard->Release();
    lpKeyboard = NULL;
  }

  if (lpMouse != NULL) {
    lpMouse->Unacquire();
    lpMouse->Release();
    lpMouse = NULL;
  }

  if (lpDInput != NULL) {
    lpDInput->Release();
    lpDInput = NULL;
  }

  if (ComBuf != NULL) {
    delete [] ComBuf;
    ComBuf = NULL;
  }

  initialized=0;

  FONLINE_LOG("FEngine Clear complete\n");
}

void FOnlineEngine::ClearCritters() //!Cvet
{
  for(crit_map::iterator it=critters.begin();it!=critters.end();it++)
  {
    hexField.RemoveCrit((*it).second);
    delete (*it).second;
  }
  critters.clear();

  lpChosen=NULL;
}

void FOnlineEngine::RemoveCritter(CritterID remid) //!Cvet
{
  crit_map::iterator it=critters.find(remid);
  if(it==critters.end()) return;

  hexField.RemoveCrit((*it).second);
  delete (*it).second;
  critters.erase(it);

  if(lpChosen->id==remid) lpChosen=NULL;
}

// карта игрока
int FOnlineEngine::Console()
{
  // 1100
  RECT r4={0,50,200,300};
  RECT r5={200,50,400,300};
  RECT r6={400,50,600,300};

  char str[256],str1[256],str2[256];

  wsprintf(str1,"Ваши Координаты x=%d,y=%d, rit=%d, \ntx=%d, ty=%d\n.  мыша x=%d, y=%d"
  ,lpChosen->hex_x,lpChosen->hex_y,lpChosen->rit,TargetX,TargetY,cur_x,cur_y);

  wsprintf(str2,"Анимационная инфа: оружие %d, cur_id %d, cur_ox %d, cur_oy %d",lpChosen->weapon,lpChosen->cur_id,lpChosen->cur_ox,lpChosen->cur_oy);
//  wsprintf(str,"Сетевые данные игроков: движение:%d, присоединился:%d, сказал:%d, вышел:%d"
//  ,LstMoveId,LstAddCritId,LstSayCritId,LstDelCritId);
//  wsprintf(str,"Pwleft:%d, Phbegin:%d"
//  ,hexField.Pwleft , hexField.Phbegin);

  wsprintf(str,"R=%d,G=%d,B=%d---Время:%d:%d",dayR,dayG,dayB,Game_Hours,Game_Mins);

  fnt.RenderText(r4,str1,FT_CENTERX,D3DCOLOR_XRGB(255,240,0));
  fnt.RenderText(r5,str2,FT_CENTERX,D3DCOLOR_XRGB(255,240,0));
  fnt.RenderText(r6,str,FT_CENTERX,D3DCOLOR_XRGB(255,240,0));
  return 1;
}

int FOnlineEngine::Render()
{
//PROCESS
  static TICK LastCall=GetTickCount();
  static uint16_t call_cnt=0;

  if((GetTickCount()-LastCall)>=1000)
  {
    Game_FPS=call_cnt;
    call_cnt=0;
    LastCall=GetTickCount();
  }
  else call_cnt++;

  ParseInput();
  if(!initialized || islost) return 0;

  //Инициализация сети
  if(state==STATE_INIT_NET)
  {
    if(!InitNet())
    {
      SetCur(CUR_DEFAULT);
      LogMsg=17;
      return 0;
    }
    if(IsScreen(SCREEN_REGISTRATION )) Net_SendCreatePlayer(&New_cr);
    if(IsScreen(SCREEN_LOGIN    )) Net_SendLogIn(opt_login, opt_pass);
  }

  //Parse NET
  if(state!=STATE_DISCONNECT)
    ParseSocket();

//!Cvet логин/пасс
  if(IsScreen(SCREEN_LOGIN    )) { ShowLogIn();     return 1; }
//!Cvet Регистрация
  if(IsScreen(SCREEN_REGISTRATION )) { ShowRegistration();  return 1; }

  if(state==STATE_DISCONNECT) { SetScreen(SCREEN_LOGIN); return 0; }

  if(IsScreen(SCREEN_GLOBAL_MAP)) { GmapProcess(); GmapDraw(); return 1; }

  if(!hexField.IsMapLoaded()) return 1;
  if(!lpChosen) return 1;

  hexField.Scroll();

  if(opt_dbgclear) lpDevice->Clear(0,NULL,D3DCLEAR_TARGET,D3DCOLOR_XRGB(0,255,0),1.0,0);

  ChosenProcess();

  CCritter* crit=NULL;
  for(crit_map::iterator it=critters.begin();it!=critters.end();it++)
  {
    crit=(*it).second;
    crit->Process();
    if(crit->human==false && crit->cur_step<4)
    {
      if(crit->cur_anim==NULL && crit->next_step[crit->cur_step]!=0xFF)
      {
        hexField.MoveCritter(crit,crit->next_step[crit->cur_step]);
        crit->Move(crit->next_step[crit->cur_step]);
        crit->cur_step++;
      }
    }
  }

  hexField.ProcessObj();

//RENDER
  lpDevice->BeginScene();

  ProccessDayTime(); //!Cvet

  hexField.DrawMap2();

  spriteManager.Flush();
  for(crit_map::iterator it=critters.begin();it!=critters.end();it++)
    (*it).second->DrawText(&fnt);

//!Cvet++++++++
  IntDraw(); //отрисовка интерфейса
  if(IsScreen(SCREEN_INVENTORY )) InvDraw(); //отрисовка инвентаря
  if(IsScreen(SCREEN_DIALOG_NPC)) DlgDraw(); //отрисовка диалога
  if(IsScreen(SCREEN_LOCAL_MAP )) LmapDraw(); //отрисовка мини-карты
  if(IsScreen(SCREEN_SKILLBOX  )) SboxDraw(); //Skillbox
  if(IsScreen(SCREEN_MENU_OPTION)) MoptDraw(); //Menu option
  if(IsLMenu()) LMenuDraw(); //отрисовка LMenu
//!Cvet--------

  spriteManager.Flush();
  if(cmn_console)
  {
    Console();

    RECT r2 = {
      10,
      300,
      screen_width[opt_screen_mode],
      screen_height[opt_screen_mode]
    };
    char verstr[256];
    wsprintf(verstr,"|2130771712 by |4278255615 Gamers |2130771712 from |4294967040 FOdev\n\n"
      "|4278190335 version %s\n\n"
      "|4294901760 fps: %d",FULLVERSTR,Game_FPS);
    fnt.RenderText(r2,verstr,FT_COLORIZE,D3DCOLOR_XRGB(255,248,0));
  }

  if(newplayer) if((GetTickCount()-LastCall)>=1000) newplayer=0;

//отрисовка курсора
  CurDraw();

  spriteManager.Flush();

  lpDevice->EndScene();

  if(lpDevice->Present(NULL,NULL,NULL,NULL)==D3DERR_DEVICELOST)
  {
    FONLINE_LOG("D3dDevice is lost\n");
    DoLost();
  }

  return 1;
}

void FOnlineEngine::ParseInput()
{
  if(dilost) RestoreDI();
  if(dilost) return;

  DIDEVICEOBJECTDATA didod[DI_BUF_SIZE];  // Receives buffered data
  DWORD dwElements;
  HRESULT hr;
  int i;

//!Cvet +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if(IsCur(CUR_WAIT))
  {
    dwElements = DI_BUF_SIZE;
    hr = lpKeyboard->GetDeviceData( sizeof(DIDEVICEOBJECTDATA),
                     didod, &dwElements, 0 );
    if(hr!=DI_OK)
    {
      dilost=1;
      FONLINE_LOG("WAIT ParseInput keyboard> %s\n",(char*)DXGetErrorString8(hr));
      return;
    }

    for(i=0;i<dwElements;i++)
    {
//      DI_ONDOWN( DIK_ESCAPE , NetDiscon(); return );
    }

    dwElements = DI_BUF_SIZE;
    hr = lpMouse->GetDeviceData( sizeof(DIDEVICEOBJECTDATA),
                     didod, &dwElements, 0 );
    if(hr!=DI_OK)
    {
      dilost=1;
      FONLINE_LOG("WAIT ParseInput mouse> %s\n",(char*)DXGetErrorString8(hr));
      return;
    }

    for(i=0;i<dwElements;i++)
    {
      DI_ONMOUSE( DIMOFS_X, cur_x+=didod[i].dwData*opt_mouse_speed );
      DI_ONMOUSE( DIMOFS_Y, cur_y+=didod[i].dwData*opt_mouse_speed );
    }
  }
  else
  {
  //логин/пасс
    if(IsScreen(SCREEN_LOGIN)) LogInput();
  //регистрация
    if(IsScreen(SCREEN_REGISTRATION)) RegInput();

    if(IsScreen(SCREEN_GLOBAL_MAP)) GmapInput();
  }

  if(IsScreen(SCREEN_LOGIN) || IsScreen(SCREEN_REGISTRATION) || IsScreen(SCREEN_GLOBAL_MAP) || IsCur(CUR_WAIT))
  {
/*  if(!opt_fullscr) убрал
    {
      RECT r;
      GetWindowRect(hWnd,&r);
      POINT p;
      GetCursorPos(&p);
      cur_x=p.x-(r.left+2);
      cur_y=p.y-(r.top+22);
    }
*/
    // FIXME[20.12.2012 alex]: better use unsigned int, but it causes
    //   mouse to jump from top to the bottom of the window, since a
    //   negative mouse coordinate is casted to unsigned int during
    //   comparison with modeHeight and then it is assigned modeHeight
    //   value.
    int modeWidth = screen_width[opt_screen_mode];
    int modeHeight = screen_height[opt_screen_mode];

    if(cur_x > modeWidth) cur_x = modeWidth;
    if(cur_x < 0) cur_x = 0;
    if(cur_y > modeHeight) cur_y = modeHeight;
    if(cur_y < 0) cur_y = 0;

    return;
  }
//!Cvet -------------------------------------------------------------

  dwElements = DI_BUF_SIZE;
  hr = lpKeyboard->GetDeviceData( sizeof(DIDEVICEOBJECTDATA),
                   didod, &dwElements, 0 );
  if(hr!=DI_OK)
  {
    dilost=1;
    FONLINE_LOG("ParseInput keyboard> %s\n",(char*)DXGetErrorString8(hr));
    return;
  }

//!Cvet +++
  static TICK tick_press=GetTickCount();
  static int time_press=1000;
  static uint16_t last_key=NULL;
  if(last_key && GetTickCount()>tick_press+time_press)
  {
    if(!GetChar(last_key,ed_str,&cur_edit,EDIT_LEN,lang,ShiftDwn)) last_key=NULL;
    tick_press=GetTickCount();
    if((time_press-=time_press/2)<30) time_press=30;
  }
//!Cvet ---

  for(i=0;i<dwElements;i++)
  {
  //  DI_ONDOWN( DIK_ESCAPE , DestroyWindow(hWnd);islost=1;return );//Exit from program
//    DI_ONDOWN( DIK_ESCAPE , NetDiscon(); return );
    //DI_ONDOWN( DIK_ESCAPE , NetDiscon(); return; );

    DI_ONDOWN( DIK_RETURN ,
      if(edit_mode)
      {
        if(!ed_str[0])
          edit_mode=0;
        else
          Net_SendText(ed_str);

        ed_str[0]=0;
        cur_edit=0;
      }
      else
      {
        edit_mode=1;
        ed_str[0]=0;
        cur_edit=0;
      }
    );

    if(edit_mode)
    {
      int fnd=0;
      uint16_t tst;
      for(tst=0;tst<256;tst++) //!Cvet было DI_BUF_SIZE
      {
        DI_ONDOWN(tst, last_key=NULL; if(GetChar(tst,ed_str,&cur_edit,EDIT_LEN,lang,ShiftDwn)) {fnd=1;break;});
        DI_ONUP(tst, last_key=NULL;);
      }
      if(fnd) { last_key=tst; tick_press=GetTickCount(); time_press=600; break; }
    }
    else if(!CtrlDwn)
    {
      DI_ONDOWN( DIK_LEFT ,cmn_di_left=1 );
      DI_ONDOWN( DIK_RIGHT ,cmn_di_right=1 );
      DI_ONDOWN( DIK_UP ,cmn_di_up=1 );
      DI_ONDOWN( DIK_DOWN ,cmn_di_down=1 );

      DI_ONUP( DIK_LEFT ,cmn_di_left=0 );
      DI_ONUP( DIK_RIGHT ,cmn_di_right=0 );
      DI_ONUP( DIK_UP ,cmn_di_up=0 );
      DI_ONUP( DIK_DOWN ,cmn_di_down=0 );
    }
    else
    {
      DI_ONDOWN( DIK_LEFT , lpChosen->RotateCounterClockwise(); Net_SendDir() );
      DI_ONDOWN( DIK_RIGHT, lpChosen->RotateClockwise(); Net_SendDir() );
    }

    DI_ONDOWN( DIK_T ,hexField.SwitchShowTrack() ); //!Cvet
    DI_ONDOWN( DIK_G ,hexField.SwitchShowHex() );
    DI_ONDOWN( DIK_R ,hexField.SwitchShowRain() ); //!Cvet

//    DI_ONUP( DIK_Q ,opt_scroll_delay-=10;FONLINE_LOG("scroll_delay=%d\n",opt_scroll_delay) );
//    DI_ONUP( DIK_W ,opt_scroll_delay+=10;FONLINE_LOG("scroll_delay=%d\n",opt_scroll_delay) );
//
//    DI_ONUP( DIK_A ,opt_scroll_step>>=1;FONLINE_LOG("scroll_step=%d\n",opt_scroll_step) );
//    DI_ONUP( DIK_S ,opt_scroll_step<<=1;FONLINE_LOG("scroll_step=%d\n",opt_scroll_step) );

    DI_ONDOWN( DIK_RCONTROL ,CtrlDwn=1;
      if(opt_change_lang==CHANGE_LANG_RCTRL) lang=(lang==LANG_RUS)?LANG_ENG:LANG_RUS;
    );
    DI_ONUP( DIK_RCONTROL ,CtrlDwn=0 );
    DI_ONDOWN( DIK_LCONTROL ,CtrlDwn=1 );
    DI_ONUP( DIK_LCONTROL ,CtrlDwn=0 );

    DI_ONDOWN( DIK_LMENU ,AltDwn=1 );
    DI_ONUP( DIK_LMENU ,AltDwn=0 );
    DI_ONDOWN( DIK_RMENU ,AltDwn=1 );
    DI_ONUP( DIK_RMENU ,AltDwn=0 );

    DI_ONDOWN( DIK_LSHIFT ,ShiftDwn=1;
      if(CtrlDwn && opt_change_lang==CHANGE_LANG_CTRL_SHIFT) lang=(lang==LANG_RUS)?LANG_ENG:LANG_RUS;
      if(AltDwn && opt_change_lang==CHANGE_LANG_ALT_SHIFT) lang=(lang==LANG_RUS)?LANG_ENG:LANG_RUS;
    );
    DI_ONUP( DIK_LSHIFT ,ShiftDwn=0 );
    DI_ONDOWN( DIK_RSHIFT ,ShiftDwn=1;
      if(CtrlDwn && opt_change_lang==CHANGE_LANG_CTRL_SHIFT) lang=(lang==LANG_RUS)?LANG_ENG:LANG_RUS;
      if(AltDwn && opt_change_lang==CHANGE_LANG_ALT_SHIFT) lang=(lang==LANG_RUS)?LANG_ENG:LANG_RUS;
    );
    DI_ONUP( DIK_RSHIFT ,ShiftDwn=0 );
/*
    DI_ONUP( DIK_5 ,cmn_show_tiles=!cmn_show_tiles );
    DI_ONUP( DIK_6 ,cmn_show_roof=!cmn_show_roof );
    DI_ONUP( DIK_7 ,cmn_show_scen=!cmn_show_scen );
    DI_ONUP( DIK_8 ,cmn_show_walls=!cmn_show_walls );
    DI_ONUP( DIK_9 ,cmn_show_items=!cmn_show_items );
    DI_ONUP( DIK_0 ,cmn_show_crit=!cmn_show_crit );

    DI_ONDOWN( DIK_3 , Game_Time=0;);
    DI_ONDOWN( DIK_1 , Game_Time+=20;);
    DI_ONDOWN( DIK_2 , Game_Time-=20;);
*/
//    DI_ONDOWN( DIK_1 , opt_light+=5;);
//    DI_ONDOWN( DIK_2 , opt_light-=5;);

//    DI_ONDOWN( DIK_M ,
//      SetScreenCastling(SCREEN_MAIN,SCREEN_GLOBAL_MAP);
//    );

    DI_ONDOWN( DIK_O , cmn_console=!cmn_console;);
//    DI_ONDOWN( DIK_I ,
//      SetScreenCastling(SCREEN_MAIN,SCREEN_INVENTORY);
//    );

    //!Cvet временное оружие - проверка на читерство
//    DI_ONDOWN( DIK_C,
//      lpChosen->AddObject(44,1100,10000000,0,0);
//    );

  }

  //formouse
  dwElements = DI_BUF_SIZE;
  hr = lpMouse->GetDeviceData( sizeof(DIDEVICEOBJECTDATA),
                   didod, &dwElements, 0 );
  if(hr!=DI_OK)
  {
    dilost=1;
    FONLINE_LOG("ParseInput mouse> %s\n",(char*)DXGetErrorString8(hr));
    return;
  }
//!Cvet ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  if(!lpChosen) return;
  //ChosenMouseInput();
//если живой
  if(lpChosen->cond==COND_LIFE)
  {
    for(i=0;i<dwElements;i++)
    {
      DI_ONMOUSE( DIMOFS_X, cur_x+=didod[i].dwData*opt_mouse_speed );
      DI_ONMOUSE( DIMOFS_Y, cur_y+=didod[i].dwData*opt_mouse_speed );

      if(IsLMenu()) LMenuMouseMove();

      DI_ONDOWN( DIMOFS_BUTTON1,
        if(IsScreen(SCREEN_MAIN))
        {
          if(IsCur(CUR_DEFAULT)  || IsCur(CUR_MOVE))
            if((cur_x>=IntObject[0])&&(cur_y>=IntObject[1])&&
              (cur_x<=IntObject[2])&&(cur_y<=IntObject[3]))
            {
              if(lpChosen->a_obj->object->type==OBJ_TYPE_WEAPON)
              {
                lpChosen->rate_object++;
                if(lpChosen->rate_object>lpChosen->a_obj->object->p[OBJ_WEAP_COUNT_ATTACK]) lpChosen->rate_object=1;
              }
            }
        }
      );

      DI_ONUP( DIMOFS_BUTTON1,
        if(IsScreen(SCREEN_MAIN))
        {
          SetCurCastling(CUR_DEFAULT,CUR_MOVE);

          if(IsCur(CUR_USE_OBJECT)) SetChosenAction(ACTION_ACT_UNACT_OBJECT);
        }
      );

      DI_ONDOWN( DIMOFS_BUTTON0,
        if(IsScreen(SCREEN_MAIN))
        {
          //засекаем время для менюшки
          if(IsCur(CUR_DEFAULT))
          {
            LMenu_try_activated=true;
            LMenu_start_time=GetTickCount();
          }

          if(!IntMouseDown())
          {
          //управляем чузеном
            //ходим
            if(IsCur(CUR_MOVE))
            {
              if(GetMouseTile(cur_x,cur_y))
              {
                if(PathMoveX==TargetX && PathMoveY==TargetY)
                  lpChosen->movementType=MOVE_RUN;
                else
                  lpChosen->movementType=MOVE_WALK;

                PathMoveX=TargetX;
                PathMoveY=TargetY;
                SetChosenAction(ACTION_MOVE);
              }
            }
            //используем объект
            if(IsCur(CUR_USE_OBJECT))
            {
              if((curTargetCrit=GetMouseCritter(cur_x,cur_y)))
              {
                tosendTargetCrit=curTargetCrit;
                SetChosenAction(ACTION_USE_OBJ_ON_CRITTER);
              }
              else if((curTargetObj=GetMouseItem(cur_x,cur_y)))
              {
                tosendTargetObj=curTargetObj;
                SetChosenAction(ACTION_USE_OBJ_ON_ITEM);
              }

            }
            //используем навык
            if(IsCur(CUR_USE_SKILL))
            {
              ;;
            }
          }
        }

        if(IsScreen(SCREEN_INVENTORY )) InvMouseDown();
        if(IsScreen(SCREEN_DIALOG_NPC)) DlgMouseDown();
        if(IsScreen(SCREEN_LOCAL_MAP )) LmapMouseDown();
        if(IsScreen(SCREEN_GLOBAL_MAP)) GmapMouseDown();
        if(IsScreen(SCREEN_SKILLBOX  )) SboxMouseDown();
        if(IsScreen(SCREEN_SKILLBOX  )) SboxMouseDown();
        if(IsScreen(SCREEN_MENU_OPTION))MoptMouseDown();
      );

      DI_ONUP( DIMOFS_BUTTON0,
        if(IsScreen(SCREEN_MAIN    )) IntMouseUp();
        if(IsScreen(SCREEN_INVENTORY )) InvMouseUp();
        if(IsScreen(SCREEN_DIALOG_NPC)) DlgMouseUp();
        if(IsScreen(SCREEN_LOCAL_MAP )) LmapMouseUp();
        if(IsScreen(SCREEN_GLOBAL_MAP)) GmapMouseUp();
        if(IsScreen(SCREEN_SKILLBOX  )) SboxMouseUp();
        if(IsScreen(SCREEN_MENU_OPTION))MoptMouseUp();

        if(IsLMenu()) LMenuMouseUp();
        LMenu_try_activated=false;
      );

      if(IsScreen(SCREEN_DIALOG_NPC)) DlgMouseMove();
      if(IsScreen(SCREEN_INVENTORY )) InvMouseMove();
      if(IsScreen(SCREEN_LOCAL_MAP )) LmapMouseMove();
      if(IsScreen(SCREEN_SKILLBOX  )) SboxMouseMove();
    }

    if(LMenu_try_activated==true) LMenuTryCreate();
    if(IsScreen(SCREEN_MAIN)) IntMouseMove();
  }
  else
  {
    for(i=0;i<dwElements;i++)
    {
      DI_ONMOUSE( DIMOFS_X, cur_x+=didod[i].dwData*opt_mouse_speed );
      DI_ONMOUSE( DIMOFS_Y, cur_y+=didod[i].dwData*opt_mouse_speed );
    }
  }
//!Cvet ----------------------------------------------------------

/*  if(!opt_fullscr) //!Cvet закомм.
  {
    RECT r;
    GetWindowRect(hWnd,&r);
    POINT p;
    GetCursorPos(&p);
    cur_x=p.x-(r.left+2);
    cur_y=p.y-(r.top+22);
  }*/

  // FIXME[20.12.2012 alex]: better use unsigned int, but it causes
  //   mouse to jump from top to the bottom of the window, since a
  //   negative mouse coordinate is casted to unsigned int during
  //   comparison with modeHeight and then it is assigned modeHeight
  //   value.
  int modeWidth = screen_width[opt_screen_mode];
  int modeHeight = screen_height[opt_screen_mode];
  if(cur_x > modeWidth) cur_x = modeWidth;
  if(cur_x < 0) cur_x = 0;
  if(cur_y > modeHeight) cur_y = modeHeight;
  if(cur_y < 0) cur_y = 0;

  if(IsScreen(SCREEN_MAIN) && !IsLMenu())
  {
    if(cur_x >= modeWidth)
    {
      cur_x = modeWidth;
      cmn_di_mright=1;
      cur=cur_right;
    }
    else cmn_di_mright=0;

    if(cur_x<=0)
    {
      cur_x=0;
      cmn_di_mleft=1;
      cur=cur_left;
    }
    else cmn_di_mleft=0;

    if(cur_y >= modeHeight)
    {
      cur_y = modeHeight;
      cmn_di_mdown=1;
      cur=cur_down;
    }
    else cmn_di_mdown=0;

    if(cur_y<=0)
    {
      cur_y=0;
      cmn_di_mup=1;
      cur=cur_up;
    }
    else cmn_di_mup=0;

    if    (cmn_di_mright  && cmn_di_mup ) cur=cur_ru;
    else if (cmn_di_mleft && cmn_di_mup ) cur=cur_lu;
    else if (cmn_di_mright  && cmn_di_mdown ) cur=cur_rd;
    else if (cmn_di_mleft && cmn_di_mdown ) cur=cur_ld;

    if(cmn_di_mright || cmn_di_mleft || cmn_di_mup || cmn_di_mdown) return;

    if(IsCur(CUR_DEFAULT  )) cur=cur_def;
    if(IsCur(CUR_MOVE   )) cur=cur_move;
    if(IsCur(CUR_USE_OBJECT )) cur=cur_use_o;
    if(IsCur(CUR_USE_SKILL  )) cur=cur_use_s;
  }
//!Cvet ++++++++
  if(IsScreen(SCREEN_INVENTORY))
    if(cur_hold)
      cur=cur_hold;
    else
      cur=cur_hand;

  if(IsScreen(SCREEN_DIALOG_NPC)) cur=cur_def;

  if(IsLMenu()) cur=cur_ru; //!!!!!!!
//!Cvet --------
}

void FOnlineEngine::Restore()
{
  if(!initialized || !islost) return;
  FONLINE_LOG("Restoring...\n");
  FONLINE_LOG("параметры режима.....");
  D3DDISPLAYMODE d3ddm;
  HRESULT hr=lpD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT,&d3ddm);
  if(hr!=D3D_OK){
    ReportErrorMessage("GetAdapterDisplayMode",(char*)DXGetErrorString8(hr));
    return;
  }
  FONLINE_LOG("OK\n");

  D3DPRESENT_PARAMETERS d3dpp;
  ZeroMemory(&d3dpp,sizeof(d3dpp));
  d3dpp.Windowed=opt_fullscr?0:1;
  d3dpp.SwapEffect=D3DSWAPEFFECT_DISCARD;
  d3dpp.Flags=D3DPRESENTFLAG_LOCKABLE_BACKBUFFER; //??? lockable need
  if(!opt_fullscr) {
    d3dpp.BackBufferFormat=d3ddm.Format;
  } else {
    size_t modeWidth = screen_width[opt_screen_mode];
    size_t modeHeight = screen_height[opt_screen_mode];
    d3dpp.BackBufferWidth=modeWidth;
    d3dpp.BackBufferHeight=modeHeight;
    d3dpp.BackBufferFormat=D3DFMT_A8R8G8B8;
    if(!opt_vsync) d3dpp.FullScreen_PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
  }

  hexField.PreRestore();
  spriteManager.PreRestore();
  fnt.PreRestore();
  hr=lpDevice->Reset(&d3dpp);
  if(hr!=D3D_OK)
  {
    //ReportErrorMessage("Device Reset","Не могу перезапустить устройство");
    FONLINE_LOG("Не могу перезапустить устройство\n");
    return;
  }
  lpDevice->Clear(0,NULL,D3DCLEAR_TARGET,D3DCOLOR_XRGB(0,0,0),1.0,0);


  FONLINE_LOG("Установка режимов.....");
  lpDevice->SetRenderState(D3DRS_LIGHTING,FALSE);//выключаем свет
  lpDevice->SetRenderState(D3DRS_ZENABLE, FALSE); // Disable Z-Buffer
  lpDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); // Disable Culling
  //включаем прозрачность - Alpha blending
  lpDevice->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
  lpDevice->SetRenderState( D3DRS_SRCBLEND,   D3DBLEND_SRCALPHA );
  lpDevice->SetRenderState( D3DRS_DESTBLEND,  D3DBLEND_INVSRCALPHA );
  //включаем alpha testing
  lpDevice->SetRenderState( D3DRS_ALPHATESTENABLE,  TRUE );
  lpDevice->SetRenderState( D3DRS_ALPHAREF,         0x08 );
  lpDevice->SetRenderState( D3DRS_ALPHAFUNC,  D3DCMP_GREATEREQUAL );


  //линейная фильтрация
  lpDevice->SetTextureStageState(0,D3DTSS_MINFILTER,D3DTEXF_LINEAR);
  lpDevice->SetTextureStageState(0,D3DTSS_MAGFILTER,D3DTEXF_LINEAR);

  lpDevice->SetTextureStageState(0,D3DTSS_COLOROP ,D3DTOP_MODULATE2X);
//  lpDevice->SetTextureStageState(0,D3DTSS_ALPHAOP ,D3DTOP_SELECTARG1);
  lpDevice->SetTextureStageState(0,D3DTSS_ALPHAOP ,D3DTOP_MODULATE); //!Cvet


  lpDevice->SetVertexShader(D3DFVF_VERTEX_FORMAT);
  FONLINE_LOG("OK\n");

  spriteManager.SetColor((0xFF000000)|((dayR+opt_light)<<16)|((dayG+opt_light)<<8)|(dayB+opt_light)); //!Cvet
  spriteManager.PostRestore(); //именно в таком порядке. ибо spriteManager раньше должен создать IB
  hexField.PostRestore();
  fnt.PostRestore(spriteManager.GetVB(),spriteManager.GetIB());

  FONLINE_LOG("Restoring complete\n");
  islost=0;
  cmn_lost=0;
}

void FOnlineEngine::RestoreDI()
{
  if(!initialized || !lpKeyboard) return;
//  FONLINE_LOG("Restoring DI...\n");

  //возвращаем контроль клавиатуре
  HRESULT hr=lpKeyboard->Acquire();
//  if(hr==DI_OK) FONLINE_LOG("RestoringDI Keyboard OK\n");
//    else FONLINE_LOG("RestoringDI Keyboard error %s\n",(char*)DXGetErrorString8(hr));

  hr=lpMouse->Acquire();
//  if(hr==DI_OK) FONLINE_LOG("RestoringDI Mouse OK\n");
//    else FONLINE_LOG("RestoringDI Mouse error %s\n",(char*)DXGetErrorString8(hr));
//  cur_x=320;cur_y=240;
//  if(!opt_fullscr)
//  {
//    RECT r;
//    GetWindowRect(hWnd,&r);
//    SetCursorPos(r.left+2+320,r.top+22+240);
//  }else SetCursorPos(320,240);
  dilost=0;
}

int FOnlineEngine::InitNet()
{
  FONLINE_LOG("Network init...\n");

  state=STATE_DISCONNECT;

  WSADATA WsaData;
  if(WSAStartup(0x0101,&WsaData))
  {
    ReportErrorMessage("FOnlineEngine::InitNet","WSAStartup error!");
    return 0;
  }

  remote.sin_family=AF_INET;
  remote.sin_port=htons(opt_rport);
  if((remote.sin_addr.s_addr=inet_addr(opt_rhost.c_str()))==-1)
  {
    hostent *h=gethostbyname(opt_rhost.c_str());

    if(!h)
    {
      ReportErrorMessage("FOnlineEngine::InitNet","cannot resolve remote host %s!",opt_rhost);
      return 0;
    }

    memcpy(&remote.sin_addr,h->h_addr,sizeof(in_addr));
  }

  #pragma chMSG("Потом убрать отсюда и ввести экран подключения")
  if(!NetCon()) return 0;

  state=STATE_CONN;

  FONLINE_LOG("Network init OK\n");

  return 1;
}

int FOnlineEngine::NetCon()
{
  FONLINE_LOG("Connecting to server %s:%d\n",opt_rhost,opt_rport);
  sock=socket(AF_INET,SOCK_STREAM,0);
  if(connect(sock,(sockaddr*)&remote,sizeof(SOCKADDR_IN)))
  {
    ReportErrorMessage("FOnlineEngine::NetCon","Не могу подключиться к серверу игры!\r\n");
    return 0;
  }
  FONLINE_LOG("Connecting OK\n");

  zstrm.zalloc = zlib_alloc;
  zstrm.zfree = zlib_free;
  zstrm.opaque = NULL;

  if(inflateInit(&zstrm)!=Z_OK)
  {
    ReportErrorMessage("FOnlineEngine::NetCon","InflateInit error!\r\n");
    return 0;
  }
  zstrmok=1;

  return 1;
  // отладка сетевых сообщений
  FONLINE_LOG("Net_Con");
}

void FOnlineEngine::NetDiscon()
{
  FONLINE_LOG("Отсоединение ");

  if(zstrmok) inflateEnd(&zstrm);
  closesocket(sock);
  state=STATE_DISCONNECT;

  FONLINE_LOG("--> Итоговая статистика сжатия трафика: %d -> %d\n",stat_com,stat_decom);

  ClearCritters(); //!Cvet
  hexField.UnLoadMap(); //!Cvet
}

void FOnlineEngine::ParseSocket(uint16_t wait)
{
  timeval tv;
  tv.tv_sec=wait;
  tv.tv_usec=0;

  if(sock==-1) return;

  FD_ZERO(&read_set);
  FD_ZERO(&write_set);
  FD_ZERO(&exc_set);

  FD_SET(sock,&read_set);
  //FD_SET(sock,&write_set);
  //FD_SET(sock,&exc_set);

  select(0,&read_set,&write_set,&exc_set,&tv);

  if(FD_ISSET(sock,&read_set))
  {
    if(!NetInput())
    {
    //  ReportErrorMessage("FOnlineEngine::ParseSocket","Игровой сервер разорвал связь!\r\n");
      FONLINE_LOG("FOnlineEngine::ParseSocket","Игровой сервер разорвал связь!\r\n");
      sock=-1;
      state=STATE_DISCONNECT;
      return;
    }
  }

  NetProcess();

  NetOutput();
}

void FOnlineEngine::NetProcess()
{
  if(!bin.writePosition) return;

  MessageType msg;

  while(bin.NeedProcess())
  {
    bin >> msg;

    switch(msg)
    {
    case NETMSG_LOGINOK:
      state=STATE_LOGINOK;
      FONLINE_LOG("Аунтефикация пройдена\n");
      LogMsg=0;
      break;
    case NETMSG_ADDCRITTER:
      Net_OnAddCritter();
      break;
    case NETMSG_REMOVECRITTER:
      Net_OnRemoveCritter();
      break;
    case NETMSG_CRITTERTEXT:
      Net_OnCritterText();
      break;

//!Cvet +++++++++++++++++++++++++++++++++++++++++++
    case NETMSG_XY:
      Net_OnChosenXY();
      break;
    case NETMSG_ALL_PARAMS:
      Net_OnChosenParams();
      break;
    case NETMSG_PARAM:
      Net_OnChosenParam();
      break;
    case NETMSG_ADD_OBJECT:
      Net_OnChosenAddObject();
      break;
    case NETMSG_LOGMSG:
      Net_OnChosenLogin();
      break;
    case NETMSG_TALK_NPC:
      Net_OnChosenTalk();
      break;

    case NETMSG_GAME_TIME:
      Net_OnGameTime();
      break;

    case NETMSG_LOADMAP:
      Net_OnLoadMap();
      break;
    case NETMSG_MAP:
      Net_OnMap();
      break;
    case NETMSG_GLOBAL_INFO:
      Net_OnGlobalInfo();
      break;

    case NETMSG_ADD_OBJECT_ON_MAP:
      Net_OnAddObjOnMap();
      break;
    case NETMSG_CHANGE_OBJECT_ON_MAP:
      Net_OnChangeObjOnMap();
      break;
    case NETMSG_REMOVE_OBJECT_FROM_MAP:
      Net_OnRemObjFromMap();
      break;

    case NETMSG_CRITTER_ACTION:
      Net_OnCritterAction();
      break;
    case NETMSG_CRITTER_MOVE:
      Net_OnCritterMove();
      break;
    case NETMSG_CRITTER_DIR:
      Net_OnCritterDir();
      break;
//!Cvet -------------------------------------------

    case NETMSG_NAMEERR:
      FONLINE_LOG("Name: %s: ERR!\n",opt_name);
      state=STATE_DISCONNECT;
      break;
    default:
      FONLINE_LOG("Wrong MSG: %d!\n",msg);
      //state=STATE_DISCONNECT;
      bin.Reset();
      return;
    }
  }
  bin.Reset();
}

/*void FOnlineEngine::Net_SendName(char* name)
{
  FONLINE_LOG("Net_SendName...");

  MessageType msg=NETMSG_NAME;

  bout << msg;
  bout.Write(name,MAX_NAME);
  for(int i=0;i<5;i++)
    bout.Write(opt_cases[i],MAX_NAME);
  uint8_t gen=opt_gender[0];
  bout << gen;

  state=STATE_NAMESEND;
  // отладка сетевых сообщений
  FONLINE_LOG("OK\n");
}*/
//!Cvet ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void FOnlineEngine::Net_SendLogIn(char* login, char* pass)
{
  FONLINE_LOG("Net_SendLogIn...");

  MessageType msg=NETMSG_LOGIN;

  bout << msg;
  bout.Write(login,MAX_LOGIN);
  bout.Write(pass,MAX_LOGIN);

  LogMsg=10;
  // отладка сетевых сообщений
  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_SendCreatePlayer(crit_info* newcr)
{
  int bi;
  FONLINE_LOG("Посылаем данные на сервер...\n");

//ФОРМИРОВАНИЕ ЗАПРОСА
  MessageType msg=NETMSG_CREATE_CLIENT;
  bout << msg;
  bout.Write(newcr->login,MAX_LOGIN);
  bout.Write(newcr->pass,MAX_LOGIN);
  bout.Write(newcr->name,MAX_NAME);
  for(bi=0; bi<5; bi++)
    bout.Write(newcr->cases[bi],MAX_NAME);
  //SPECIAL
  bout << newcr->st[ST_STRENGHT ];
  bout << newcr->st[ST_PERCEPTION ];
  bout << newcr->st[ST_ENDURANCE  ];
  bout << newcr->st[ST_CHARISMA ];
  bout << newcr->st[ST_INTELLECT  ];
  bout << newcr->st[ST_AGILITY  ];
  bout << newcr->st[ST_LUCK   ];
  //возраст
  bout << newcr->st[ST_AGE];
  //пол
  bout << newcr->st[ST_GENDER];

//  FONLINE_LOG("log:|%s|\n",newcr->login);
//  FONLINE_LOG("pas:|%s|\n",newcr->pass);
  FONLINE_LOG("OK\n");
}
//!Cvet ----------------------------------------------------------------
void FOnlineEngine::Net_SendText(char* str)
{
  FONLINE_LOG("Net_SendText...");

  uint16_t len=strlen(str);
  if(len>=MAX_TEXT) len=MAX_TEXT;
  MessageType msg=NETMSG_TEXT;

  bout << msg;
  bout << len;
  bout.Write(str,len);

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_SendDir()
{
  FONLINE_LOG("Net_SendDir...");

  MessageType msg=NETMSG_DIR;

  bout << msg;
  bout << lpChosen->cur_dir;
  // отладка сетевых сообщений
  FONLINE_LOG("OK\n");
}

//!Cvet +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void FOnlineEngine::Net_SendMove(uint8_t* dir) //переделал
{
  FONLINE_LOG("Net_SendMove...");

  uint8_t how_move=lpChosen->Move(dir[0]);

  if(how_move==MOVE_ERROR)
  {
    FONLINE_LOG("FALSE\n");
    return;
  }
  hexField.MoveCritter(lpChosen,dir[0]);

//  lpChosen->AccamulateCur_offs();

  uint16_t move_params=0;

  for(int p=0;p<5;p++)
    switch(dir[p])
    {
    case 0: SetBit(move_params, PMOVE_0<<(3 * p), true); break;
    case 1: SetBit(move_params, PMOVE_1<<(3 * p), true); break;
    case 2: SetBit(move_params, PMOVE_2<<(3 * p), true); break;
    case 3: SetBit(move_params, PMOVE_3<<(3 * p), true); break;
    case 4: SetBit(move_params, PMOVE_4<<(3 * p), true); break;
    case 5: SetBit(move_params, PMOVE_5<<(3 * p), true); break;
    case 0xFF: SetBit(move_params, 0x7<<(3 * p), true);  break;
    default: return;
    }

  if(how_move==MOVE_RUN) SetBit(move_params, PMOVE_RUN, true);

//  if(lpChosen->hex_x!=PathMoveX || lpChosen->hex_y!=PathMoveY)
//    SETFLAG(move_params,);

  MessageType msg=NETMSG_SEND_MOVE;

  bout << msg;
  bout << move_params;

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_SendUseSkill(uint8_t skill, uint32_t targ_id, uint8_t ori)
{
  FONLINE_LOG("Отправка использования скилла...");

  MessageType msg=NETMSG_SEND_USE_SKILL;

  bout << msg;
  bout << skill;
  bout << targ_id;
  bout << ori;

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_SendUseObject(uint8_t type_target, uint32_t targ_id, uint8_t crit_ori, uint8_t crit_num_action, uint8_t crit_rate_object)
{
  FONLINE_LOG("Отправка действия над предметом...");

  MessageType msg=NETMSG_SEND_USE_OBJECT;

  bout << msg;
  bout << type_target;
  bout << targ_id;
  bout << crit_ori;
  bout << crit_num_action;
  bout << crit_rate_object;

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_SendPickObject(HexTYPE targ_x, HexTYPE targ_y, uint16_t id_sobj)
{
  FONLINE_LOG("Отправка взаимодействия с предметом на земле...");

  MessageType msg=NETMSG_SEND_PICK_OBJECT;

  bout << msg;
  bout << targ_x;
  bout << targ_y;
  bout << id_sobj;

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_SendChangeObject(uint32_t idobj, uint8_t num_slot)
{
  FONLINE_LOG("Отправка смены предмета id=%d, slot=%d...",idobj, num_slot);

  MessageType msg=NETMSG_SEND_CHANGE_OBJECT;

  bout << msg;
  bout << idobj;
  bout << num_slot;

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_SendTalk(CritterID id_to_talk, uint8_t answer)
{
  FONLINE_LOG("Отпрака сообщения по общению с НПЦ id_NPC=%d, ответ=%d...", id_to_talk, answer);

  MessageType msg=NETMSG_SEND_TALK_NPC;

  bout << msg;
  bout << id_to_talk;
  bout << answer;

  lpChosen->Tick_Start(TALK_MAX_TIME);

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_SendGetTime()
{
  FONLINE_LOG("Отпрака сообщения по запросу игрового времени...");

  MessageType msg=NETMSG_SEND_GET_TIME;

  bout << msg;

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_SendGiveMeMap(uint16_t map_num)
{
  FONLINE_LOG("Отпрака сообщения по запросу загрузки карты...");

  MessageType msg=NETMSG_SEND_GIVE_ME_MAP;

  bout << msg;
  bout << map_num;

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_SendLoadMapOK()
{
  FONLINE_LOG("Отпрака сообщения по успешной загрузке карты...");

  MessageType msg=NETMSG_SEND_LOAD_MAP_OK;

  bout << msg;

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_SendGiveGlobalInfo(uint8_t info_flags)
{
  FONLINE_LOG("Отпрака сообщения запросу инфы о глобале №%d...",info_flags);

  MessageType msg=NETMSG_SEND_GIVE_GLOBAL_INFO;

  bout << msg;
  bout << info_flags;

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_SendRuleGlobal(uint8_t command, uint32_t param1, uint32_t param2)
{
  FONLINE_LOG("Отправка запроса управлением группой...");

  MessageType msg=NETMSG_SEND_RULE_GLOBAL;

  bout << msg;
  bout << command;
  bout << param1;
  bout << param2;

  FONLINE_LOG("OK\n");
}
//!Cvet -----------------------------------------------------------------------------

void FOnlineEngine::Net_OnAddCritter()
{
  FONLINE_LOG("Присоединился новый игрок...");
  crit_info info;

  info.a_obj=&info.def_obj1;
  info.a_obj_arm=&info.def_obj2;

  uint16_t id_obj;

  bin >> info.id;
  bin >> info.base_type;

  bin >> id_obj;
  info.a_obj->object=all_s_obj[id_obj];

  bin >> id_obj;
  info.a_obj_arm->object=all_s_obj[id_obj];

  bin >> info.x;
  bin >> info.y;
  bin >> info.ori;
  bin >> info.st[ST_GENDER];
  bin >> info.cond;
  bin >> info.cond_ext;
  bin >> info.flags;
  bin.Read(info.name,MAX_NAME);
  FONLINE_LOG("Имя:%s\n",info.name);
  //FONLINE_LOG("Имя: %s, cond=%d,cond_ext=%d\n",info.name,info.cond,info.cond_ext);
  info.name[MAX_NAME]=0;

  for(int i=0;i<5;i++)
  {
    bin.Read(info.cases[i],MAX_NAME);
    info.cases[i][MAX_NAME]=0;
  }

  if(bin.IsError())
  {
    FONLINE_LOG("Net_OnAddCritter","Wrong MSG data forNet_OnAddCritter!\n"); //ReportErrorMessage было
    state=STATE_DISCONNECT;
    return;
  }

  LstAddCritId=info.id; // отладка
  strcpy(newbie,info.name); // погоняло прибывшего
  newplayer=1; // флаг входа нового игрока

  CCritter* pc=AddCritter(&info);

//  else AddMess(COLOR_TEXT_DEFAULT,"Вы увидели %s",info.cases[2]);

//  hexField.PostRestore();

  FONLINE_LOG("Загрузка игрока закончена. %s, id=%d.\n",info.name,info.id);
}

void FOnlineEngine::Net_OnRemoveCritter()
{
  CritterID remid;

  bin >> remid;

  if(bin.IsError())
  {
    ReportErrorMessage("Net_OnRemoveCritter","Wrong MSG data forNet_OnRemoveCritter!\n");
    state=STATE_DISCONNECT;
    return;
  }
  LstDelCritId=remid;// отладка
  FONLINE_LOG("%d клиент получил приказ сервера отключиться...",remid);

  crit_map::iterator it=critters.find(remid);
  if(it==critters.end()) return;
  CCritter* prem=(*it).second;

  FONLINE_LOG("убираем с hexfield...");
  hexField.RemoveCrit(prem);

  FONLINE_LOG("убираем с critters...");
  delete (*it).second;

  critters.erase(it);
  FONLINE_LOG("OK\n",remid);
}

void FOnlineEngine::Net_OnCritterText()
{
  CritterID crid;
  uint8_t how_say; //!Cvet

  bin >> crid;

  bin >> how_say;

  if(bin.IsError())
  {
    ReportErrorMessage("Net_OnRemoveCritter","Wrong MSG data forNet_OnRemoveCritter!\n");
    state=STATE_DISCONNECT;
    return;
  }
  LstSayCritId=crid;// отладка

  uint16_t len;
  char str[MAX_TEXT+256];

  bin >> len;

  if(bin.IsError() || len>(MAX_TEXT+256))
  {
    FONLINE_LOG("Wrong MSG data or too long forProcess_GetText!(len=%d)\n",len);
    state=STATE_DISCONNECT;
    return;
  }

  bin.Read(str,len);
  str[len]=0;

  if(bin.IsError())
  {
    FONLINE_LOG("Wrong MSG data forProcess_GetText - partial recv!\n");
    state=STATE_DISCONNECT;
    return;
  }

//!Cvet ++++++++++++++++++++++++++++++++
  uint32_t text_color=COLOR_TEXT_DEFAULT;
  switch(how_say)
  {
  default:
  case SAY_NORM:
    break;
  case SAY_SHOUT:
    text_color=COLOR_TEXT_SHOUT;
    break;
  case SAY_EMOTE:
    text_color=COLOR_TEXT_EMOTE;
    break;
  case SAY_WHISP:
    text_color=COLOR_TEXT_WHISP;
    break;
  case SAY_SOCIAL:
    text_color=COLOR_TEXT_SOCIAL;
    break;
  }

  AddMess(text_color,"%s",str);

  if(!IsScreen(SCREEN_GLOBAL_MAP))
//!Cvet --------------------------------
  {
    crit_map::iterator it=critters.find(crid);
    CCritter* pcrit=NULL;
    if(it!=critters.end())
      pcrit=(*it).second;

    if(pcrit) pcrit->SetText(str,text_color);
  }

  FONLINE_LOG("Net_OnCritterText");
}

void FOnlineEngine::Net_OnCritterDir()
{
  FONLINE_LOG("Установка игроку ");

  CritterID crid;
  uint8_t new_dir;

  bin >> crid;
  bin >> new_dir;

  FONLINE_LOG("%d направления %d...",crid,new_dir);

  if(bin.IsError())
  {
    FONLINE_LOG("Wrong MSG data for Net_OnCritterDir!\n");
    state=STATE_DISCONNECT;
    return;
  }

  if(new_dir>5)
  {
    FONLINE_LOG("неверное значение\n");
    return;
  }

  crit_map::iterator it=critters.find(crid);
  CCritter* pcrit=NULL;
  if(it==critters.end())
  {
    FONLINE_LOG("ненайден криттер\n");
    return;
  }
  pcrit=(*it).second;

  if(pcrit->cur_dir==new_dir) return;

  pcrit->SetDirection(new_dir);

  FONLINE_LOG("OK\n");
}

//!Cvet +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void FOnlineEngine::Net_OnCritterMove()
{
  FONLINE_LOG("Ходит игрок id=");

  CritterID crid=0;
//  uint8_t dir=0;
  uint8_t how_move=0;
  HexTYPE new_x=0;
  HexTYPE new_y=0;

  uint16_t move_params=0;

  //присланные данные
  bin >> crid;
  bin >> move_params;

  bin >> new_x;
  bin >> new_y;

  if(bin.IsError())
  {
    FONLINE_LOG("Wrong MSG data for Net_OnCritterMove!\n");
    state=STATE_DISCONNECT;
    return;
  }

  // dir=FLAG(move_params,BIN8(00000111));

  how_move = MOVE_WALK;
  if ((move_params & PMOVE_RUN) != 0) {
    how_move = MOVE_RUN;
  }

  // проверка данных
  if (new_x >= MAXTILEX || new_y >= MAXTILEY) {
    return;
  }

  if (!crid) {
    return;
  }
  // if(dir>5) return;

  FONLINE_LOG("%d...", crid);
  //  FONLINE_LOG("%d dir=%d movementType=%d...", crid, dir, how_move);

  LstMoveId=crid; // для отладки движения

  crit_map::iterator it=critters.find(crid);
  CCritter* pcrit=NULL;
  if(it==critters.end()) return;
  pcrit=(*it).second;

  pcrit->movementType=how_move;

  if(pcrit->cur_step && pcrit->cur_step<4 && (pcrit->next_step[0]== (move_params & 0x7))) {
    pcrit->cur_step--;
  } else {
    hexField.MoveCritter(pcrit,new_x,new_y);
    if (!pcrit->Move(pcrit->cur_dir)) {
      pcrit->SetAnimation();
    }
    pcrit->cur_step=0;
  }

  if ((pcrit->next_step[0] = ((move_params>>0x3) & 0x7))>PMOVE_5) pcrit->next_step[0]=0xFF;
  if ((pcrit->next_step[1] = ((move_params>>0x6) & 0x7))>PMOVE_5) pcrit->next_step[1]=0xFF;
  if ((pcrit->next_step[2] = ((move_params>>0x9) & 0x7))>PMOVE_5) pcrit->next_step[2]=0xFF;
  if ((pcrit->next_step[3] = ((move_params>>0xC) & 0x7))>PMOVE_5) pcrit->next_step[3]=0xFF;

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_OnCritterAction()
{
  FONLINE_LOG("Пинг: %d\n",GetTickCount()-Ping);

  CritterID crid;
  uint8_t num_action;
  uint16_t id_st_obj;
  uint16_t id_st_obj_arm;
  uint8_t rate_obj;
  uint8_t ori;

  bin >> crid;
  bin >> num_action; //номер действия
  bin >> id_st_obj; //id статического оружия в руке
  bin >> id_st_obj_arm; //id статического оружия в слот армор
  bin >> rate_obj; //режим использования
  bin >> ori; //ориентация действия

  FONLINE_LOG("Игрок id=%d действует...",crid);

  if(bin.IsError())
  {
    ReportErrorMessage("Net_OnRemoveCritter","Wrong MSG data forNet_OnCritterAction!\n");
    state=STATE_DISCONNECT;
    return;
  }

//находим криттера
  crit_map::iterator it=critters.find(crid);
  CCritter* pcrit=NULL;
  if(it!=critters.end()) pcrit=(*it).second;
  else return;
//находим объект в руке
  stat_map::iterator it2=all_s_obj.find(id_st_obj);
  stat_obj* pobj=NULL;
  if(it2!=all_s_obj.end()) pobj=(*it2).second;
  else return;
  pcrit->a_obj->object=pobj;
//находим предмет в слот армор
  it2=all_s_obj.find(id_st_obj_arm);
  stat_obj* pobj_arm=NULL;
  if(it2!=all_s_obj.end()) pobj_arm=(*it2).second;
  else return;
  pcrit->a_obj_arm->object=pobj_arm;

  pcrit->cur_dir=ori;

  FONLINE_LOG("Net_OnCritterAction - act=%d, rate=%d\n", num_action, rate_obj);

//обробатываем действия
//достование/скрывание объекта
  if(num_action==ACT_SHOW_OBJ)
  {
    if(!pobj->p[OBJ_TIME_SHOW]) return;

    pcrit->RefreshWeap();

    if(pobj->type==OBJ_TYPE_WEAPON)
      pcrit->Action(3,pobj->p[OBJ_TIME_SHOW]);
    else
      pcrit->Action(ANIM2_USE,pobj->p[OBJ_TIME_SHOW]);

    return;
  }

  if(num_action==ACT_HIDE_OBJ)
  {
    if(!pobj->p[OBJ_TIME_HIDE]) return;

    pcrit->RefreshWeap();

    if(pobj->type==OBJ_TYPE_WEAPON)
      pcrit->Action(3,pobj->p[OBJ_TIME_HIDE]);
    else
      pcrit->Action(ANIM2_USE,pobj->p[OBJ_TIME_HIDE]);

    return;
  }
//активация/деактивация/использование объекта
  if(num_action==ACT_ACTIVATE_OBJ)
  {
    if(pobj->type!=OBJ_TYPE_WEAPON) return;
    if(!pobj->p[OBJ_WEAP_TIME_ACTIV]) return;

    pcrit->cond_ext=COND_LIFE_ACTWEAP;

    pcrit->Action(8,pobj->p[OBJ_WEAP_TIME_ACTIV]);

    return;
  }

  if(num_action==ACT_DACTIVATE_OBJ)
  {
    if(pobj->type!=OBJ_TYPE_WEAPON) return;
    if(!pobj->p[OBJ_WEAP_TIME_UNACTIV]) return;

    pcrit->cond_ext=COND_LIFE_NONE;

    pcrit->Action(9,pobj->p[OBJ_WEAP_TIME_UNACTIV]);

    return;
  }

  if(num_action==ACT_USE_OBJ)
  {
//    pcrit->cond_ext=COND_LIFE_USEOBJ;

    if(pobj->type==OBJ_TYPE_WEAPON)
    {
      switch(rate_obj)
      {
      case 0:
        break;
      case 1:
        pcrit->Action(pobj->p[OBJ_WEAP_PA_ANIM2],pobj->p[OBJ_WEAP_PA_TIME]);
        break;
      case 2:
        pcrit->Action(pobj->p[OBJ_WEAP_SA_ANIM2],pobj->p[OBJ_WEAP_SA_TIME]);
        break;
      case 3:
        pcrit->Action(pobj->p[OBJ_WEAP_TA_ANIM2],pobj->p[OBJ_WEAP_TA_TIME]);
        break;
      default:
        break;
      }
    }
    else
      pcrit->Action(ANIM2_USE,pobj->p[rate_obj]);

    return;
  }

  if(num_action==ACT_DROP_OBJ)
  {
    pcrit->Action(ANIM2_SIT,ACTTIME_DROP_OBJ);

    return;
  }

  if(num_action==ACT_PICK_OBJ_UP)
  {
    pcrit->Action(ANIM2_USE,ACTTIME_PICK_OBJ);

    return;
  }

  if(num_action==ACT_PICK_OBJ_DOWN)
  {
    pcrit->Action(ANIM2_SIT,ACTTIME_PICK_OBJ);

    return;
  }
//обновление
  if(num_action==ACT_REFRESH)
  {
    pcrit->SetAnimation();

    return;
  }
//принятие повреждения
  if(num_action==ACT_DEFEAT)
  {
  //  if(!pcrit->IsFree()) return;

    switch(rate_obj)
    {
    case ACT_DEFEAT_MISS:
      if(pcrit->weapon==1)
        pcrit->Action(14,0);
      else
        pcrit->Action(5,0);
      break;
    case ACT_DEFEAT_FRONT:
      if(pcrit->weapon==1)
        pcrit->Action(15,0);
      else
        pcrit->Action(5,0);
      break;
    case ACT_DEFEAT_REAR:
      pcrit->Action(16,0);
      break;
    case ACT_DEFEAT_KO_FRONT:
      break;
    case ACT_DEFEAT_KO_REAR:
      break;
    default:
      break;
    }

    return;
  }
//смена брони
  if(num_action==ACT_CHANGE_ARM)
  {
    pcrit->SetAnimation();

    return;
  }
//смерть
  if(num_action==ACT_DEAD)
  {
    pcrit->cond=COND_DEAD;
    pcrit->cond_ext=rate_obj;
    pcrit->weapon=2;

    switch(pcrit->cond_ext)
    {
    case COND_DEAD_NORMAL_UP:
      pcrit->Action(1,1000);//15
      break;
    case COND_DEAD_NORMAL_DOWN:
      pcrit->Action(2,1000);//16
      break;
    case COND_DEAD_CR_NORMAL_UP:
      pcrit->Action(4,1000);
      break;
    case COND_DEAD_BRUST:
      pcrit->Action(7,1000);
      break;
    case COND_DEAD_CR_BRUST:
      pcrit->Action(6,1000);
      break;
    default:
      pcrit->Action(1,1000);
      break;
    }

    return;
  }

  if(num_action==ACT_DISCONNECT)
  {
    pcrit->flags=pcrit->flags | FCRIT_DISCONNECT;

    strcat(pcrit->name,"_off");
    for(int in=0; in<5; in++)
      strcat(pcrit->cases[in],"_off");
  }
}

void FOnlineEngine::Net_OnChosenLogin()
{
  bin >> LogMsg;

  SetCur(CUR_DEFAULT);
}

void FOnlineEngine::Net_OnChosenXY() // направление надо передавать?!cvet
{
  FONLINE_LOG("Проверка или установка места...");

  HexTYPE Chex_x;
  HexTYPE Chex_y;
  uint8_t Cori;

  bin >> Chex_x;
  bin >> Chex_y;
  bin >> Cori;

  if(bin.IsError())
  {
    ReportErrorMessage("Net_OnCritterNewXY","Wrong MSG data forNet_OnCritterNewXY!\n");
    state=STATE_DISCONNECT;
    return;
  }

  if(!lpChosen->IsFree())
  {
    FONLINE_LOG("пропуск. Чезен занят\n");
    return;
  }

  if(Chex_x>=MAXTILEX || Chex_y>=MAXTILEY || Cori>5)
  {
    FONLINE_LOG("ОШИБКА в принятых данных |x=%d,y=%d,ori=%d|\n",Chex_x,Chex_y,Cori);
    return;
  }

  if(lpChosen->cur_dir!=Cori)
  {
    lpChosen->cur_dir=Cori;
    FONLINE_LOG("Установлено/Исправлено направление...");
  }

  if(lpChosen->hex_x!=Chex_x || lpChosen->hex_y!=Chex_y)
  {
    hexField.TransitCritter(lpChosen,Cori,Chex_x,Chex_y,true);
    FONLINE_LOG("Установлено/Исправлено местоположение...");

    SetChosenAction(ACTION_NONE);
    lpChosen->SetAnimation();
  }

  // отладка сетевых сообщений
  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_OnChosenParams()
{
  FONLINE_LOG("Присланны параметры...");

  uint8_t type_param=0;
  uint8_t all_send_params=0;
  uint8_t num_param=0;
  int go=0;

  bin >> type_param;
  bin >> all_send_params;

  switch (type_param)
  {
  case TYPE_STAT:
    for(go=0; go<ALL_STATS; go++) lpChosen->st[go]=0;
    for(go=0; go<all_send_params; go++)
    {
      bin >> num_param;
      bin >> lpChosen->st[num_param];

      params_str_map::iterator it=stats_str_map.find(num_param);
//      if(it!=stats_str_map.end()) AddMess(COLOR_TEXT_DEFAULT,"Стат %s = %d",(*it).second.c_str(),lpChosen->st[num_param]);
    }
    break;
  case TYPE_SKILL:
    for(go=0; go<ALL_SKILLS; go++) lpChosen->sk[go]=0;
    for(go=0; go<all_send_params; go++)
    {
      bin >> num_param;
      bin >> lpChosen->sk[num_param];

      params_str_map::iterator it=skills_str_map.find(num_param);
//      if(it!=skills_str_map.end()) AddMess(COLOR_TEXT_DEFAULT,"Навык %s = %d",(*it).second.c_str(),lpChosen->sk[num_param]);
    }
    break;
  case TYPE_PERK:
    for(go=0; go<ALL_PERKS; go++) lpChosen->pe[go]=0;
    for(go=0; go<all_send_params; go++)
    {
      bin >> num_param;
      bin >> lpChosen->pe[num_param];

      params_str_map::iterator it=perks_str_map.find(num_param);
//      if(it!=perks_str_map.end()) AddMess(COLOR_TEXT_DEFAULT,"Перк %s",(*it).second.c_str());
    }
    break;
  default:
    FONLINE_LOG("Ошибка. Неправильный тип параметра №%d\n", type_param);
    return;
  }

  if(bin.IsError())
  {
    FONLINE_LOG("Bin Error - Net_OnChosenParams\n");
    state=STATE_DISCONNECT;
    return;
  }

  FONLINE_LOG("Загрузка параметров закончена - всего параметров типа %d прислано %d\n", type_param, all_send_params);
}

void FOnlineEngine::Net_OnChosenParam()
{
  FONLINE_LOG("Прислан параметр...");

  uint8_t type_param=0;
  uint8_t num_param=0;
  uint16_t old_count=0;
  params_str_map::iterator it;

  bin >> type_param;
  bin >> num_param;

  switch (type_param)
  {
  case TYPE_STAT:
    old_count=lpChosen->st[num_param];

    bin >> lpChosen->st[num_param];

//    it=stats_str_map.find(num_param);
//    if(it!=stats_str_map.end())
//    {
//      if(lpChosen->st[num_param]>old_count)
//        AddMess(COLOR_TEXT_DEFAULT,"Стат %s увеличился на %d (всего:%d)",(*it).second.c_str(),lpChosen->st[num_param]-old_count,lpChosen->st[num_param]);
//      else if(lpChosen->st[num_param]<old_count)
//        AddMess(COLOR_TEXT_DEFAULT,"Стат %s уменьшился на %d (всего:%d)",(*it).second.c_str(),old_count-lpChosen->st[num_param],lpChosen->st[num_param]);
//    }

    break;
  case TYPE_SKILL:
    old_count=lpChosen->sk[num_param];

    bin >> lpChosen->sk[num_param];

    it=skills_str_map.find(num_param);
    if(it!=skills_str_map.end())
    {
      if(lpChosen->sk[num_param]>old_count)
        AddMess(COLOR_TEXT_DEFAULT,"Навык %s увеличился на %d (всего:%d)",(*it).second.c_str(),lpChosen->sk[num_param]-old_count,lpChosen->sk[num_param]);
      else if(lpChosen->sk[num_param]<old_count)
        AddMess(COLOR_TEXT_DEFAULT,"Навык %s уменьшился на %d (всего:%d)",(*it).second.c_str(),old_count-lpChosen->sk[num_param],lpChosen->sk[num_param]);
    }

    break;
  case TYPE_PERK:
    old_count=lpChosen->pe[num_param];

    bin >> lpChosen->pe[num_param];

//    it=perks_str_map.find(num_param);
//    if(it!=perks_str_map.end())
//    {
//      if(lpChosen->pe[num_param]>old_count)
//        AddMess(COLOR_TEXT_DEFAULT,"Добавлен перк %s",(*it).second.c_str());
//      else if(lpChosen->sk[num_param]<old_count)
//        AddMess(COLOR_TEXT_DEFAULT,"Снят перк %s",(*it).second.c_str());
//    }

    break;
  default:
    FONLINE_LOG("Ошибка. Неправильный тип параметра №%d\n", type_param);
    return;
  }

  if(bin.IsError())
  {
    FONLINE_LOG("Bin Error - Net_OnChosenParam\n");
    state=STATE_DISCONNECT;
    return;
  }

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_OnChosenAddObject()
{
  FONLINE_LOG("Добавляеться предмет...");

  uint32_t id_d;
  uint16_t id_s;
  uint8_t aslot;
  uint32_t time_wear;
  uint32_t broken_info;


  bin >> id_d;
  bin >> id_s;
  bin >> aslot;
  bin >> time_wear;
  bin >> broken_info;

  if(bin.IsError())
  {
    ReportErrorMessage("Net_OnCritterNewXY","Wrong MSG data for Net_OnChosenAddObject!\n");
    state=STATE_DISCONNECT;
    return;
  }

  lpChosen->AddObject(aslot,id_d,broken_info,time_wear,all_s_obj[id_s]);

  FONLINE_LOG("id_dyn=%d, id_stat=%d, a_slot=%d, wear=%d, broken=%d\n",
    id_d, id_s, aslot, time_wear, broken_info);
}

void FOnlineEngine::Net_OnAddObjOnMap()
{
  FONLINE_LOG("Добавляеться предмет на земле...");

  HexTYPE obj_x;
  HexTYPE obj_y;
  uint16_t obj_id;
  uint16_t tile_flags;

  bin >> obj_x;
  bin >> obj_y;
  bin >> obj_id;
  bin >> tile_flags;

  if(bin.IsError())
  {
    ReportErrorMessage("Net_OnCritterNewXY","Wrong MSG data for Net_OnAddObjOnMap!\n");
    state=STATE_DISCONNECT;
    return;
  }

  if(obj_x>=MAXTILEX || obj_y>=MAXTILEY)
  {
    FONLINE_LOG("ошибка границ\n");
    return;
  }

  stat_map::iterator so=all_s_obj.find(obj_id);
  if(so==all_s_obj.end())
  {
    FONLINE_LOG("не найден предмет в списке прототипов\n");
    return;
  }

  if(!hexField.AddObj((*so).second,obj_x,obj_y,tile_flags))
  {
    FONLINE_LOG("ошибка добавления\n");
    return;
  }

  //AddMess(0xFF00FFFF,"Прилетел предметик %d",(*so).second->id);

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_OnChangeObjOnMap()
{
  FONLINE_LOG("Изменяется предмет на земле...");

  HexTYPE obj_x;
  HexTYPE obj_y;
  uint16_t obj_id;
  uint16_t tile_flags;

  bin >> obj_x;
  bin >> obj_y;
  bin >> obj_id;
  bin >> tile_flags;

  if(bin.IsError())
  {
    ReportErrorMessage("Net_OnCritterNewXY","Wrong MSG data for Net_OnChangeObjOnMap!\n");
    state=STATE_DISCONNECT;
    return;
  }

  if(obj_x>=MAXTILEX || obj_y>=MAXTILEY)
  {
    FONLINE_LOG("ошибка границ\n");
    return;
  }

  stat_map::iterator so=all_s_obj.find(obj_id);
  if(so==all_s_obj.end())
  {
    FONLINE_LOG("не найден предмет в списке прототипов\n");
    return;
  }

  hexField.ChangeObj((*so).second,obj_x,obj_y,tile_flags);

  //AddMess(0xFFAAAAFF,"Предметик изменился %d",(*so).second->id);

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_OnRemObjFromMap()
{
  FONLINE_LOG("Удаляеться предмет с земли...");

  HexTYPE obj_x;
  HexTYPE obj_y;
  uint16_t obj_id;

  bin >> obj_x;
  bin >> obj_y;
  bin >> obj_id;

  if(bin.IsError())
  {
    ReportErrorMessage("Net_OnCritterNewXY","Wrong MSG data for Net_OnRemObjFromMap!\n");
    state=STATE_DISCONNECT;
    return;
  }

  if(obj_x>=MAXTILEX || obj_y>=MAXTILEY)
  {
    FONLINE_LOG("ошибка границ\n");
    return;
  }

  stat_map::iterator so=all_s_obj.find(obj_id);
  if(so==all_s_obj.end())
  {
    FONLINE_LOG("не найден предмет в списке прототипов\n");
    return;
  }

  hexField.DelObj((*so).second,obj_x,obj_y);

  //AddMess(0xFFFFAAAA,"Улетел предметик %d",(*so).second->id);

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_OnChosenTalk()
{
  FONLINE_LOG("Новая диалоговая ветка...");

  uint32_t main_text;
  uint32_t answer[MAX_ANSWERS];

  bin >> all_answers;

  if(!all_answers)
  {
    SetScreen(SCREEN_MAIN);
    lpChosen->Tick_Null();
    FONLINE_LOG("диалог закончен...OK\n");
    return;
  }

  bin >> main_text;
  if(!LoadDialogFromFile(tosendTargetCrit->id,main_text,text_dialog)) FONLINE_LOG("Ошибка загрузки диалога №%d...", main_text);

  for(int ct=0; ct<all_answers; ct++)
  {
    bin >> answer[ct];
    if(!LoadDialogFromFile(tosendTargetCrit->id,answer[ct],text_answer[ct]))
      FONLINE_LOG("Ошибка загрузки ответа №%d...", answer[ct]);
  }

  lpChosen->Tick_Start(TALK_MAX_TIME);

  SetScreen(SCREEN_DIALOG_NPC);

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_OnGameTime()
{
  FONLINE_LOG("Сведения о игровом времени...");

  bin >> Game_Time;
  bin >> Game_Day;
  bin >> Game_Month;
  bin >> Game_Year;

  Game_CurTime=Game_Time*60*1000;
  Game_TimeTick=GetTickCount();

  SetDayTime(Game_CurTime);

  FONLINE_LOG("OK\n");
}

void FOnlineEngine::Net_OnLoadMap()
{
  FONLINE_LOG("Приказ смены карты...\n");

  uint16_t num_map;
  char name_map[30];

  bin >> num_map;

  SetCur(CUR_WAIT);

  ClearCritters();

  if(!num_map) //global
  {
    GmapNullParams();
    SetScreen(SCREEN_GLOBAL_MAP);
    Net_SendLoadMapOK();

    FONLINE_LOG("Global OK\n");
    return;
  }

  sprintf(name_map,"%d.map",num_map);

  if(hexField.LoadMap(name_map)) //local
  {
    FONLINE_LOG("Loading local map\n");
    Net_SendLoadMapOK();
    SetScreen(SCREEN_MAIN);
  }
  else
    Net_SendGiveMeMap(num_map);

  FONLINE_LOG("Local OK\n");
}

void FOnlineEngine::Net_OnMap()
{

}

void FOnlineEngine::Net_OnGlobalInfo()
{
  FONLINE_LOG("Сведения о глобале...");

  uint8_t info_flags=0;

  bin >> info_flags;

  if(!info_flags) return;

  if((info_flags & GM_INFO_CITIES) != 0)
  {
    FONLINE_LOG("локации(");

    for(std::vector<GM_city*>::iterator it_c=gm_cities.begin();it_c!=gm_cities.end();++it_c)
      delete (*it_c);
    gm_cities.clear();

    uint16_t count_cities;

    bin >> count_cities;

    FONLINE_LOG("%d)...",count_cities);

    for(int i=0;i<count_cities;++i)
    {
      uint16_t city_num;
      uint16_t city_x;
      uint16_t city_y;
      uint8_t city_radius;

      bin >> city_num;
      bin >> city_x;
      bin >> city_y;
      bin >> city_radius;

      GM_city* add_city=new GM_city(city_num,city_x,city_y,city_radius);
      gm_cities.push_back(add_city);
    }
  }

  if((info_flags & GM_INFO_CRITS) != 0) {
    FONLINE_LOG("криты(");

    for(std::vector<GM_crit*>::iterator it_cr=gm_crits.begin();it_cr!=gm_crits.end();++it_cr)
      delete (*it_cr);
    gm_crits.clear();

    uint8_t count_group;
    CritterID id_crit;
    char cur_name[MAX_NAME+1];
    uint16_t flags_crit;

    bin >> count_group;

    FONLINE_LOG("%d)...",count_group);

    if(!count_group || count_group>GM_MAX_GROUP)
    {
    //  GmapNullParams();
      Net_SendGiveGlobalInfo(info_flags);
      //!!! надо еще Reset буферу сделать
      return;
    }

    for(int i=0;i<count_group;++i)
    {
      bin >> id_crit;

      //проверка id_crit

      bin.Read(cur_name,MAX_NAME);
      cur_name[MAX_NAME]=0;

      if(bin.IsError())
      {
        FONLINE_LOG("Wrong MSG data for Net_OnGlobalInfo - partial recv!\n");
        state=STATE_DISCONNECT;
        return;
      }

      //проверка cur_name

      bin >> flags_crit;

      //проверка flags_crit

      GM_crit* add_crit=new GM_crit;

      add_crit->crid=id_crit;
      strcpy(add_crit->name,cur_name);
      if ((flags_crit & FCRIT_PLAYER) != 0) add_crit->player=true;
      if ((flags_crit & FCRIT_NPC) != 0) add_crit->npc=true;
      if ((flags_crit & FCRIT_MOB) != 0) add_crit->mob=true;
      if ((flags_crit & FCRIT_DISCONNECT) != 0) add_crit->disconnect=true;
      if ((flags_crit & FCRIT_RULEGROUP) != 0) add_crit->rule=true;
      if ((flags_crit & FCRIT_CHOSEN) != 0) add_crit->chosen=true;

      if(add_crit->chosen==true && add_crit->rule==true) lock_group_contr=false;

      gm_crits.push_back(add_crit);
    }

  }

  if ((info_flags & GM_INFO_PARAM) != 0) {
    FONLINE_LOG("параметры...");

    uint16_t group_x;
    uint16_t group_y;
    uint16_t move_x;
    uint16_t move_y;
    uint8_t speed;
    int speed_x;
    int speed_y;

    bin >> group_x;
    bin >> group_y;
    bin >> move_x;
    bin >> move_y;
    bin >> speed;
    bin >> speed_x;
    bin >> speed_y;

    if(speed!=GM_SPEED_SLOW && speed!=GM_SPEED_NORM && speed!=GM_SPEED_FAST)
    {
    //  GmapNullParams();
      Net_SendGiveGlobalInfo(info_flags);
      //!!! надо еще Reset буферу сделать
      return;
    }

    GmapGroupXf=group_x;
    GmapGroupYf=group_y;
    gm_last_tick=GetTickCount();

    GmapGroupX=group_x;
    GmapGroupY=group_y;
    if(gm_process==false)
    {
      GmapMapScrX=(GmapWMap[2]-GmapWMap[0])/2+GmapWMap[0]-GmapGroupX;
      GmapMapScrY=(GmapWMap[3]-GmapWMap[1])/2+GmapWMap[1]-GmapGroupY;
    }

    GmapMoveX=move_x;
    GmapMoveY=move_y;
    GmapSpeed=speed;
    GmapSpeedX=(float)(speed_x)/1000000;
    GmapSpeedY=(float)(speed_y)/1000000;

  // XXX[27.7.2012 alex]: this code needs Vector class with a length() method
  // XXX[27.7.2012 alex]: unused variable
  // int dist=sqrt(pow(GmapMoveX-GmapGroupX,2.0)+pow(GmapMoveY-GmapGroupY,2.0));

    gm_process=true;
  }

  uint8_t end_info=0;
  bin >> end_info;

  if(end_info!=0xAA)
  {
  //  GmapNullParams();
    Net_SendGiveGlobalInfo(info_flags);
    //!!! надо еще Reset буферу сделать
    return;
  }

  FONLINE_LOG("OK\n");
}
//!Cvet -----------------------------------------------------------------------------

int FOnlineEngine::NetOutput()
{
  if(!bout.writePosition) return 1;
  int tosend=bout.writePosition;
  int sendpos=0;
  while(sendpos<tosend)
  {
    int bsent=send(sock,bout.data+sendpos,tosend-sendpos,0);
    sendpos+=bsent;
    if(bsent==SOCKET_ERROR)
    {
      ReportErrorMessage("NetOutput", "SOCKET_ERROR whilesend forserver\n");
      state=STATE_DISCONNECT;
      return 0;
    }
  }

  bout.Reset();
  // отладка сетевых сообщений
  FONLINE_LOG("NetOutput\n");

  Ping=GetTickCount();

  return 1;
}

int FOnlineEngine::NetInput() {
  int len = recv(sock,ComBuf,comlen,0);
  if (len == SOCKET_ERROR || !len) {
    //ReportErrorMessage("FOnlineEngine::NetInput","Socket error!\r\n");
    FONLINE_LOG("FOnlineEngine::NetInput - Socket error!\r\n");
    return 0;
  }

  bool rebuf=0;

  compos=len;

  while(compos==comlen)
  {
    rebuf=1;
    uint32_t newcomlen=comlen<<2;
    char * NewCOM=new char[newcomlen];
    memcpy(NewCOM,ComBuf,comlen);
    if (ComBuf != NULL) {
      delete [] ComBuf;
      ComBuf = NULL;
    }
    ComBuf=NewCOM;
    comlen=newcomlen;

    len=recv(sock,ComBuf+compos,comlen-compos,0);
    compos+=len;
  }

  if(rebuf)
    bin.EnsureWriteCapacity(comlen<<1);
  bin.Reset();

  zstrm.next_in=(UCHAR*)ComBuf;
  zstrm.avail_in=compos;
  zstrm.next_out=(UCHAR*)bin.data;
  zstrm.avail_out=bin.capacity;


  if(inflate(&zstrm,Z_SYNC_FLUSH)!=Z_OK)
  {
    ReportErrorMessage("FOnlineEngine::NetInput","Inflate error!\r\n");
    return 0;
  }

  bin.writePosition=zstrm.next_out-(UCHAR*)bin.data;

  while(zstrm.avail_in)
  {
    bin.EnsureWriteCapacity(bin.capacity<<2);

    zstrm.next_out=(UCHAR*)bin.data+bin.writePosition;
    zstrm.avail_out=bin.capacity-bin.writePosition;


    if(inflate(&zstrm,Z_SYNC_FLUSH)!=Z_OK)
    {
      ReportErrorMessage("FOnlineEngine::NetInput","Inflate continue error!\r\n");
      return 0;
    }
    bin.writePosition+=zstrm.next_out-(UCHAR*)bin.data;
  }
  FONLINE_LOG("\nrecv %d->%d bytes\n",compos,bin.writePosition);
  stat_com+=compos;
  stat_decom+=bin.writePosition;


  return 1;
}

/*
// Менеджер загрузки любых анимаций
int FOnlineEngine::LoadAnyAnims(int atype,AnyAnimData* pany)
{
  if(!pany) return 0;

  char path[1024];

  switch(atype)
  {
  case engage:
    if(pany->eng) return 1;
    pany->eng=new AnyFrames;
    strcpy(path,"endanim.frm");
    if(!(spriteManager.LoadAnyAnimation(path,PT_ART_INTRFACE,pany->eng))) return 0;
    pany->eng->ticks=800;
    break;
  default:
    return 0;
  }
  return 1;
}

// анимация интерфейса
void FOnlineEngine::IntAnim()
{
  endanim=lpAnyData->eng;
  endanim->cnt_frames=5; // счетчик
  end_id=endanim->ind[1];
  //anm_tkr=GetTickCount();
  //chng_tkr=GetTickCount();
}
*/

void FOnlineEngine::SetColor(uint8_t r,uint8_t g,uint8_t b)
{
  spriteManager.SetColor(D3DCOLOR_ARGB(255,r+opt_light,g+opt_light,b+opt_light));
  hexField.OnChangeCol();
}

// !Cvet ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void FOnlineEngine::SetColor(uint32_t color)
{
  spriteManager.SetColor(color);
  hexField.OnChangeCol();
}

CCritter* FOnlineEngine::AddCritter(crit_info* pinfo)
{
  CCritter* pcrit=new CCritter(&spriteManager);

  RemoveCritter(pinfo->id);

  critters.insert(crit_map::value_type(pinfo->id,pcrit));

  pcrit->hex_x=pinfo->x;
  pcrit->hex_y=pinfo->y;
  pcrit->base_type=pinfo->base_type;
  pcrit->st[ST_GENDER]=pinfo->st[ST_GENDER];
  pcrit->cur_dir=pinfo->ori;

  pcrit->def_obj1=pinfo->def_obj1;
  pcrit->def_obj2=pinfo->def_obj2;
  pcrit->a_obj=&pcrit->def_obj1;
  pcrit->a_obj_arm=&pcrit->def_obj2;

  pcrit->cond=pinfo->cond;
  pcrit->cond_ext=pinfo->cond_ext;
  pcrit->flags=pinfo->flags;

  pcrit->id=pinfo->id;

  pcrit->SetName(pinfo->name);
  for(int in=0; in<5; in++)
    strcpy(pcrit->cases[in],pinfo->cases[in]);

  if ((pcrit->flags & FCRIT_DISCONNECT) != 0)
  {
    strcat(pcrit->name,"_off");
    for(int in=0; in<5; in++)
      strcat(pcrit->cases[in],"_off");
  }

  pcrit->Init();

  if(!lpChosen && (pcrit->flags & FCRIT_CHOSEN) != 0)
  {
    lpChosen=pcrit;
    lpChosen->human=true;

    pcrit->def_obj1.object=all_s_obj[pcrit->base_type];
    pcrit->def_obj2.object=all_s_obj[pcrit->base_type+200];

    if(pcrit->cond==COND_LIFE && pcrit->cond_ext==COND_LIFE_ACTWEAP) SetCur(CUR_USE_OBJECT);

    hexField.FindSetCenter(pcrit->hex_x,pcrit->hex_y);

    pcrit->SetAnimation();
  }

  hexField.SetCrit(pcrit->hex_x,pcrit->hex_y,pcrit);

//FONLINE_LOG("x=%d y=%d type=%d, weap=%d\n",pcrit->hex_x,pcrit->hex_y,pcrit->type,pcrit->weapon);

  return pcrit;
}

int FOnlineEngine::GetMouseTile(int cursor_x, int cursor_y)
{
  TargetX=0;
  TargetY=0;

  if(cursor_x>=IntMain[0] && cursor_y>=IntMain[1] && cursor_x<=IntMain[2] && cursor_y<=IntMain[3]) return 0;

  return hexField.GetTilePixel(cursor_x,cursor_y,&TargetX,&TargetY);
}

CCritter* FOnlineEngine::GetMouseCritter(int cursor_x, int cursor_y)
{
  CCritter* cr;

  for(crit_map::iterator it=critters.begin();it!=critters.end();it++)
  {
    cr=(*it).second;

    if(cursor_x > cr->drect.left && cursor_x < cr->drect.right)
      if(cursor_y > cr->drect.top && cursor_y < cr->drect.bottom)
        return cr;
  }

  return NULL;
}

int FOnlineEngine::GetMouseScenery(int cursor_x, int cursor_y)
{

  return 0;
}

ItemObj* FOnlineEngine::GetMouseItem(int cursor_x, int cursor_y)
{
  return hexField.GetItemPixel(cursor_x,cursor_y);
}

int FOnlineEngine::CheckRegData(crit_info* newcr)
{
//ПРОВЕРКА ДАННЫХ
  FONLINE_LOG("Проверка данных регистрации... ");
  int bi;
  //проверка на длинну логина
  if(strlen(newcr->login)<MIN_LOGIN || strlen(newcr->login)>MAX_LOGIN)
  {
    FONLINE_LOG("Err - LOGIN\n");
    LogMsg=1;
    return 0;
  }
  //проверка на длинну пасса
  if(strlen(newcr->pass)<MIN_LOGIN || strlen(newcr->pass)>MAX_LOGIN)
  {
    FONLINE_LOG("Err - PASS\n");
    LogMsg=2;
    return 0;
  }
  //проверка на длинну имени
  if(strlen(newcr->name)<MIN_NAME || strlen(newcr->name)>MAX_NAME)
  {
    FONLINE_LOG("Err - NAME\n");
    LogMsg=13;
    return 0;
  }
  //проверка на длинну cases
  for(bi=0; bi<5; bi++)
    if(strlen(newcr->cases[bi])<MIN_NAME || strlen(newcr->cases[bi])>MAX_NAME)
    {
      FONLINE_LOG("Err - CASES%d\n",bi);
      LogMsg=14;
      return 0;
    }
  //проверка базового типа
  if(newcr->base_type<0 || newcr->base_type>2)
  {
    FONLINE_LOG("Err - БАЗОВЫЙ ТИП\n");
    LogMsg=5;
    return 0;
  }
  //проверка пола
  if(newcr->st[ST_GENDER]<0 || newcr->st[ST_GENDER]>1)
  {
    FONLINE_LOG("Err - ПОЛ\n");
    LogMsg=15;
    return 0;
  }
  //проверка возраста
  if(newcr->st[ST_AGE]<14 || newcr->st[ST_AGE]>80)
  {
    FONLINE_LOG("Err - ВОЗРАСТ\n");
    LogMsg=16;
    return 0;
  }
  //проверка SPECIAL
  if((newcr->st[ST_STRENGHT ]<1)||(newcr->st[ST_STRENGHT  ]>10)||
    (newcr->st[ST_PERCEPTION]<1)||(newcr->st[ST_PERCEPTION  ]>10)||
    (newcr->st[ST_ENDURANCE ]<1)||(newcr->st[ST_ENDURANCE ]>10)||
    (newcr->st[ST_CHARISMA  ]<1)||(newcr->st[ST_CHARISMA  ]>10)||
    (newcr->st[ST_INTELLECT ]<1)||(newcr->st[ST_INTELLECT ]>10)||
    (newcr->st[ST_AGILITY ]<1)||(newcr->st[ST_AGILITY   ]>10)||
    (newcr->st[ST_LUCK    ]<1)||(newcr->st[ST_LUCK    ]>10))
    {
      FONLINE_LOG("Err - SPECIAL №%d\n", bi);
      LogMsg=5;
      return 0;
    }
  if((newcr->st[ST_STRENGHT]+newcr->st[ST_PERCEPTION]+newcr->st[ST_ENDURANCE]+
    newcr->st[ST_CHARISMA]+newcr->st[ST_INTELLECT]+
    newcr->st[ST_AGILITY]+newcr->st[ST_LUCK])!=40)
    {
      FONLINE_LOG("Err - SPECIAL sum\n");
      LogMsg=5;
      return 0;
    }

  FONLINE_LOG("OK\n");
  return 1;
}

void FOnlineEngine::ProccessDayTime()
{
  if(GetTickCount()-Game_TimeTick<1000) return;

  Game_CurTime+=(GetTickCount()-Game_TimeTick)*TIME_MULTIPLER;
  Game_TimeTick=GetTickCount();

  SetDayTime(Game_CurTime);
}

void FOnlineEngine::SetDayTime(uint32_t time_ms)
{
  static const uint16_t DAY_MIN=24*60;

  static const uint8_t countR[]={18 ,128,103,51 };
  static const uint8_t countG[]={18 ,128,95 ,40 };
  static const uint8_t countB[]={53 ,128,86 ,29 };

  uint32_t time_day=(time_ms/1000/60)%DAY_MIN;

  Game_Hours=time_day/60;
  Game_Mins=time_day%60;

  if(time_day<(DAY_MIN/4)) //ночь
  {
    dayR=countR[0]+(float)(countR[1]-countR[0])/(DAY_MIN/4)*time_day;
    dayG=countG[0]+(float)(countG[1]-countG[0])/(DAY_MIN/4)*time_day;
    dayB=countB[0]+(float)(countB[1]-countB[0])/(DAY_MIN/4)*time_day;
  }
  else if(time_day<(DAY_MIN/2)) //утро
  {
    time_day-=(DAY_MIN/4);
    dayR=countR[1]-(float)(countR[1]-countR[2])/(DAY_MIN/4)*time_day;
    dayG=countG[1]-(float)(countG[1]-countG[2])/(DAY_MIN/4)*time_day;
    dayB=countB[1]-(float)(countB[1]-countB[2])/(DAY_MIN/4)*time_day;
  }
  else if(time_day<(DAY_MIN/4*3)) //день
  {
    time_day-=(DAY_MIN/2);
    dayR=countR[2]-(float)(countR[2]-countR[3])/(DAY_MIN/4)*time_day;
    dayG=countG[2]-(float)(countG[2]-countG[3])/(DAY_MIN/4)*time_day;
    dayB=countB[2]-(float)(countB[2]-countB[3])/(DAY_MIN/4)*time_day;
  }
  else if(time_day<DAY_MIN) //вечер
  {
    time_day-=(DAY_MIN/4*3);
    dayR=countR[3]-(float)(countR[3]-countR[0])/(DAY_MIN/4)*time_day;
    dayG=countG[3]-(float)(countG[3]-countG[0])/(DAY_MIN/4)*time_day;
    dayB=countB[3]+(float)(countB[0]-countB[3])/(DAY_MIN/4)*time_day;
  }

  static uint16_t sum_RGB=0;
  if(sum_RGB!=(dayR+dayG+dayB))
  {
    SetColor(dayR,dayG,dayB);
    sum_RGB=dayR+dayG+dayB;
  }
}

void FOnlineEngine::ChosenProcess()
{
  if(!lpChosen) return;

  int rofx=lpChosen->hex_x;
  int rofy=lpChosen->hex_y;
  if(rofx%2) rofx--;
  if(rofy%2) rofy--;

  if(hexField.hex_field[rofy][rofx].roof_id)
    cmn_show_roof=FALSE;
  else
    if(!cmn_show_roof)
    {
      cmn_show_roof=TRUE;
      hexField.RebuildTiles();
    }

  if(lpChosen->pe[PE_HIDE_MODE])
    lpChosen->alpha=0x82;
  else
    lpChosen->alpha=0xFF;

  if(!lpChosen->IsFree()) return;

  if(lpChosen->cond!=COND_LIFE) return;

  switch (*chosen_action.begin())
  {
  case ACTION_NONE:
    break;
  case ACTION_MOVE:
    if(lpChosen->cond_ext!=COND_LIFE_NONE) break;

    if(lpChosen->hex_x!=PathMoveX || lpChosen->hex_y!=PathMoveY)
    {
    //ищем направление
//FONLINE_LOG("hx=%d,px=%d,hy=%d,py=%d\n",lpChosen->hex_x,PathMoveX,lpChosen->hex_y,PathMoveY);

      uint8_t steps[FINDPATH_MAX_PATH];
      HRESULT res=hexField.FindStep(lpChosen->hex_x,lpChosen->hex_y,PathMoveX,PathMoveY,&steps[0]);
      if(res==FP_OK)
      {
        Net_SendMove(&steps[0]);
        return;
      }
    //обработка других ситуаций
      if(res==FP_DEADLOCK   ) lpChosen->SetText("Я туда не пройду",COLOR_TEXT_DEFAULT);
      if(res==FP_ERROR    ) FONLINE_LOG("!!!WORNING!!! Ошибка поиска пути\n",COLOR_TEXT_DEFAULT);
      if(res==FP_TOOFAR   ) lpChosen->SetText("Далековато",COLOR_TEXT_DEFAULT);
      if(res==FP_ALREADY_HERE ) lpChosen->SetText("Уже тут",COLOR_TEXT_DEFAULT);
    }

    break;
  case ACTION_ACT_UNACT_OBJECT:
    if(!IsCur(CUR_USE_OBJECT))
    {
      SetCur(CUR_USE_OBJECT);

      if(lpChosen->a_obj->object->type==OBJ_TYPE_WEAPON)
        if(lpChosen->a_obj->object->p[OBJ_WEAP_TIME_ACTIV])
        {
          lpChosen->Action(8,lpChosen->a_obj->object->p[OBJ_WEAP_TIME_ACTIV]);
          Net_SendUseObject(USE_OBJ_ON_CRITTER,lpChosen->a_obj->id,lpChosen->cur_dir,ACT_ACTIVATE_OBJ,0);
          lpChosen->cond_ext=COND_LIFE_ACTWEAP;
        }
    }
    else
    {
      SetCur(CUR_DEFAULT);

      if(lpChosen->a_obj->object->type==OBJ_TYPE_WEAPON)
        if(lpChosen->a_obj->object->p[OBJ_WEAP_TIME_UNACTIV])
        {
          lpChosen->Action(9,lpChosen->a_obj->object->p[OBJ_WEAP_TIME_UNACTIV]);
          Net_SendUseObject(USE_OBJ_ON_CRITTER,lpChosen->a_obj->id,lpChosen->cur_dir,ACT_DACTIVATE_OBJ,0);
          lpChosen->cond_ext=COND_LIFE_NONE;
        }
    }
    break;
  case ACTION_USE_OBJ_ON_CRITTER:
  {
    if(!tosendTargetCrit) break;

    if(lpChosen->a_obj->object->type==OBJ_TYPE_WEAPON && lpChosen==tosendTargetCrit) break;

    uint8_t AttDir=hexField.FindTarget(lpChosen->hex_x,lpChosen->hex_y,tosendTargetCrit->hex_x,tosendTargetCrit->hex_y,lpChosen->GetMaxDistance());

    if(hexField.IsShowTrack()) hexField.PostRestore();

    if(AttDir>5)
    {
      if(AttDir==FINDTARGET_BARRIER   ) lpChosen->SetText("Я туда не попаду",COLOR_TEXT_DEFAULT);
      if(AttDir==FINDTARGET_ERROR     ) FONLINE_LOG("!!!WORNING!!! Ошибка поиска пути\n",COLOR_TEXT_DEFAULT);
      if(AttDir==FINDTARGET_TOOFAR    ) lpChosen->SetText("Далековато",COLOR_TEXT_DEFAULT);
      if(AttDir==FINDTARGET_INVALID_TARG  ) lpChosen->SetText("Неправильная цель",COLOR_TEXT_DEFAULT);

      break;
    }

    lpChosen->cur_dir=AttDir;

    if(lpChosen->a_obj->object->type==OBJ_TYPE_WEAPON)
    {
      if(lpChosen->a_obj->object->p[OBJ_WEAP_TIME_ACTIV] && lpChosen->cond_ext!=COND_LIFE_ACTWEAP) break;

      float mod_TypeAttack=0;
      uint8_t attack_skill=0;
      uint8_t num_anim2=0;

      switch(lpChosen->rate_object)
      {
      case 1:
        attack_skill=lpChosen->a_obj->object->p[OBJ_WEAP_PA_SKILL];
        mod_TypeAttack=lpChosen->a_obj->object->p[OBJ_WEAP_PA_TIME]/1000;
        num_anim2=lpChosen->a_obj->object->p[OBJ_WEAP_PA_ANIM2];
        break;
      case 2:
        attack_skill=lpChosen->a_obj->object->p[OBJ_WEAP_SA_SKILL];
        mod_TypeAttack=lpChosen->a_obj->object->p[OBJ_WEAP_SA_TIME]/1000;
        num_anim2=lpChosen->a_obj->object->p[OBJ_WEAP_SA_ANIM2];
        break;
      case 3:
        attack_skill=lpChosen->a_obj->object->p[OBJ_WEAP_TA_SKILL];
        mod_TypeAttack=lpChosen->a_obj->object->p[OBJ_WEAP_TA_TIME]/1000;
        num_anim2=lpChosen->a_obj->object->p[OBJ_WEAP_TA_ANIM2];
        break;
      default:
        SetChosenAction(ACTION_NONE);
        return;
      }

      if(!num_anim2) { SetChosenAction(ACTION_NONE); return; } //@@@cheat@@@

      float mod_TypeWeap=0;
      switch(attack_skill)
      {
      case SK_UNARMED:    mod_TypeWeap=0; break;
      case SK_THROWING:   mod_TypeWeap=0.1f; break;
      case SK_MELEE_WEAPONS:  mod_TypeWeap=0.3f; break;
      case SK_SMALL_GUNS:   mod_TypeWeap=2.0f; break;
      case SK_BIG_GUNS:   mod_TypeWeap=3.2f; break;
      case SK_ENERGY_WEAPONS: mod_TypeWeap=4.5f; break;
      default:        SetChosenAction(ACTION_NONE); return;
      }

      Net_SendUseObject(USE_OBJ_ON_CRITTER,tosendTargetCrit->id,lpChosen->cur_dir,ACT_USE_OBJ,lpChosen->rate_object);

      float tick_flt=(3.01f-lpChosen->sk[attack_skill]/100+mod_TypeWeap+mod_TypeAttack)*
        (13/6-lpChosen->st[ST_AGILITY]/6)*1000;

      lpChosen->Action(num_anim2,uint16_t(tick_flt));
    }
    else
    {
      Net_SendUseObject(USE_OBJ_ON_CRITTER,tosendTargetCrit->id,lpChosen->cur_dir,ACT_USE_OBJ,lpChosen->rate_object);

      lpChosen->Action(ANIM2_USE,1000);
    }

    break;
  }
  case ACTION_USE_OBJ_ON_ITEM:
    break;
  case ACTION_USE_SKL_ON_CRITTER:
    break;
  case ACTION_USE_SKL_ON_ITEM:
    break;
  case ACTION_TALK_NPC:
    if(!tosendTargetCrit) break;

  // XXX[27.7.2012 alex]: this code needs Vector class with a length() method
    if(sqrt(pow(lpChosen->hex_x-tosendTargetCrit->hex_x,2.0)+pow(lpChosen->hex_y-tosendTargetCrit->hex_y,2.0))>TALK_NPC_DISTANCE)
    {
      PathMoveX=tosendTargetCrit->hex_x;
      PathMoveY=tosendTargetCrit->hex_y;
      if(!hexField.CutPath(lpChosen->hex_x,lpChosen->hex_y,&PathMoveX,&PathMoveY,TALK_NPC_DISTANCE-1))
      {
        SetChosenAction(ACTION_NONE);
        break;
      }

      lpChosen->movementType=MOVE_WALK;
      SetChosenAction(ACTION_MOVE);
      AddChosenAction(ACTION_TALK_NPC);
      return;
    }

    Net_SendTalk(tosendTargetCrit->id,0);
    break;
  case ACTION_PICK_OBJ:

    bool dist_1=false;

    char sx[6]={-1,-1,0,1,1, 0};
    char sy[6]={ 0, 1,1,1,0,-1};

    if(lpChosen->hex_x%2)
    {
      sy[0]--;
      sy[1]--;
      sy[3]--;
      sy[4]--;
    }

    for(int i=0;i<6;i++)
      if(lpChosen->hex_x+sx[i]==tosendTargetObj->hex_x && lpChosen->hex_y+sy[i]==tosendTargetObj->hex_y)
      {
        dist_1=true;
        break;
      }

    if(dist_1==false)
    {
      PathMoveX=tosendTargetObj->hex_x;
      PathMoveY=tosendTargetObj->hex_y;
      if(!hexField.CutPath(lpChosen->hex_x,lpChosen->hex_y,&PathMoveX,&PathMoveY,1))
      {
        SetChosenAction(ACTION_NONE);
        break;
      }

      lpChosen->movementType=MOVE_WALK;
      SetChosenAction(ACTION_MOVE);
      AddChosenAction(ACTION_PICK_OBJ);
      return;
    }

    lpChosen->cur_dir=hexField.GetDir(lpChosen->hex_x,lpChosen->hex_y,tosendTargetObj->hex_x,tosendTargetObj->hex_y);
    Net_SendDir();
    Net_SendPickObject(tosendTargetObj->hex_x,tosendTargetObj->hex_y,tosendTargetObj->sobj->id);

    if(tosendTargetObj->sobj->type==OBJ_TYPE_DOOR)
      lpChosen->Action(ANIM2_USE,ACTTIME_USE_DOOR);
    else
      lpChosen->Action(ANIM2_SIT,ACTTIME_PICK_OBJ);

    break;
  }

  PathMoveX=lpChosen->hex_x;
  PathMoveY=lpChosen->hex_y;
  EraseFrontChosenAction();
}

void FOnlineEngine::CreateParamsMaps()
{
  stats_map.insert(params_map::value_type("ST_STRENGHT",        ST_STRENGHT));
  stats_map.insert(params_map::value_type("ST_PERCEPTION",      ST_PERCEPTION));
  stats_map.insert(params_map::value_type("ST_ENDURANCE",       ST_ENDURANCE));
  stats_map.insert(params_map::value_type("ST_CHARISMA",        ST_CHARISMA));
  stats_map.insert(params_map::value_type("ST_INTELLECT",       ST_INTELLECT));
  stats_map.insert(params_map::value_type("ST_AGILITY",       ST_AGILITY));
  stats_map.insert(params_map::value_type("ST_LUCK",          ST_LUCK));
  stats_map.insert(params_map::value_type("ST_MAX_LIFE",        ST_MAX_LIFE));
  stats_map.insert(params_map::value_type("ST_MAX_COND",        ST_MAX_COND));
  stats_map.insert(params_map::value_type("ST_ARMOR_CLASS",     ST_ARMOR_CLASS));
  stats_map.insert(params_map::value_type("ST_MELEE_DAMAGE",      ST_MELEE_DAMAGE));
  stats_map.insert(params_map::value_type("ST_WEAPON_DAMAGE",     ST_WEAPON_DAMAGE));
  stats_map.insert(params_map::value_type("ST_CARRY_WEIGHT",      ST_CARRY_WEIGHT));
  stats_map.insert(params_map::value_type("ST_SEQUENCE",        ST_SEQUENCE));
  stats_map.insert(params_map::value_type("ST_HEALING_RATE",      ST_HEALING_RATE));
  stats_map.insert(params_map::value_type("ST_CRITICAL_CHANCE",   ST_CRITICAL_CHANCE));
  stats_map.insert(params_map::value_type("ST_MAX_CRITICAL",      ST_MAX_CRITICAL));
  stats_map.insert(params_map::value_type("ST_INGURE_ABSORB",     ST_INGURE_ABSORB));
  stats_map.insert(params_map::value_type("ST_LASER_ABSORB",      ST_LASER_ABSORB));
  stats_map.insert(params_map::value_type("ST_FIRE_ABSORB",     ST_FIRE_ABSORB));
  stats_map.insert(params_map::value_type("ST_PLASMA_ABSORB",     ST_PLASMA_ABSORB));
  stats_map.insert(params_map::value_type("ST_ELECTRO_ABSORB",    ST_ELECTRO_ABSORB));
  stats_map.insert(params_map::value_type("ST_EMP_ABSORB",      ST_EMP_ABSORB));
  stats_map.insert(params_map::value_type("ST_BLAST_ABSORB",      ST_BLAST_ABSORB));
  stats_map.insert(params_map::value_type("ST_INGURE_RESIST",     ST_INGURE_RESIST));
  stats_map.insert(params_map::value_type("ST_LASER_RESIST",      ST_LASER_RESIST));
  stats_map.insert(params_map::value_type("ST_FIRE_RESIST",     ST_FIRE_RESIST));
  stats_map.insert(params_map::value_type("ST_PLASMA_RESIST",     ST_PLASMA_RESIST));
  stats_map.insert(params_map::value_type("ST_ELECTRO_RESIST",    ST_ELECTRO_RESIST));
  stats_map.insert(params_map::value_type("ST_EMP_RESIST",      ST_EMP_RESIST));
  stats_map.insert(params_map::value_type("ST_BLAST_RESIST",      ST_BLAST_RESIST));
  stats_map.insert(params_map::value_type("ST_RADIATION_RESISTANCE",  ST_RADIATION_RESISTANCE));
  stats_map.insert(params_map::value_type("ST_POISON_RESISTANCE",   ST_POISON_RESISTANCE));
  stats_map.insert(params_map::value_type("ST_AGE",         ST_AGE));
  stats_map.insert(params_map::value_type("ST_GENDER",        ST_GENDER));
  stats_map.insert(params_map::value_type("ST_CURRENT_HP",      ST_CURRENT_HP));
  stats_map.insert(params_map::value_type("ST_POISONING_LEVEL",   ST_POISONING_LEVEL));
  stats_map.insert(params_map::value_type("ST_RADIATION_LEVEL",   ST_RADIATION_LEVEL));
  stats_map.insert(params_map::value_type("ST_CURRENT_STANDART",    ST_CURRENT_STANDART));

  skills_map.insert(params_map::value_type("SK_SMALL_GUNS",     SK_SMALL_GUNS));
  skills_map.insert(params_map::value_type("SK_BIG_GUNS",       SK_BIG_GUNS));
  skills_map.insert(params_map::value_type("SK_ENERGY_WEAPONS",   SK_ENERGY_WEAPONS));
  skills_map.insert(params_map::value_type("SK_UNARMED",        SK_UNARMED));
  skills_map.insert(params_map::value_type("SK_MELEE_WEAPONS",    SK_MELEE_WEAPONS));
  skills_map.insert(params_map::value_type("SK_THROWING",       SK_THROWING));
  skills_map.insert(params_map::value_type("SK_FIRST_AID",      SK_FIRST_AID));
  skills_map.insert(params_map::value_type("SK_DOCTOR",       SK_DOCTOR));
  skills_map.insert(params_map::value_type("SK_SNEAK",        SK_SNEAK));
  skills_map.insert(params_map::value_type("SK_LOCKPICK",       SK_LOCKPICK));
  skills_map.insert(params_map::value_type("SK_STEAL",        SK_STEAL));
  skills_map.insert(params_map::value_type("SK_TRAPS",        SK_TRAPS));
  skills_map.insert(params_map::value_type("SK_SCIENCE",        SK_SCIENCE));
  skills_map.insert(params_map::value_type("SK_REPAIR",       SK_REPAIR));
  skills_map.insert(params_map::value_type("SK_SPEECH",       SK_SPEECH));
  skills_map.insert(params_map::value_type("SK_BARTER",       SK_BARTER));
  skills_map.insert(params_map::value_type("SK_GAMBLING",       SK_GAMBLING));
  skills_map.insert(params_map::value_type("SK_OUTDOORSMAN",      SK_OUTDOORSMAN));

  perks_map.insert(params_map::value_type("PE_FAST_METABOLISM",   PE_FAST_METABOLISM));
  perks_map.insert(params_map::value_type("PE_BRUISER",       PE_BRUISER));
  perks_map.insert(params_map::value_type("PE_SMALL_FRAME",     PE_SMALL_FRAME));
  perks_map.insert(params_map::value_type("PE_ONE_HANDER",      PE_ONE_HANDER));
  perks_map.insert(params_map::value_type("PE_FINESSE",       PE_FINESSE));
  perks_map.insert(params_map::value_type("PE_KAMIKAZE",        PE_KAMIKAZE));
  perks_map.insert(params_map::value_type("PE_HEAVY_HANDED",      PE_HEAVY_HANDED));
  perks_map.insert(params_map::value_type("PE_FAST_SHOT",       PE_FAST_SHOT));
  perks_map.insert(params_map::value_type("PE_BLOODY_MESS",     PE_BLOODY_MESS));
  perks_map.insert(params_map::value_type("PE_JINXED",        PE_JINXED));
  perks_map.insert(params_map::value_type("PE_GOOD_NATURED",      PE_GOOD_NATURED));
  perks_map.insert(params_map::value_type("PE_CHEM_RELIANT",      PE_CHEM_RELIANT));
  perks_map.insert(params_map::value_type("PE_CHEM_RESISTANT",    PE_CHEM_RESISTANT));
  perks_map.insert(params_map::value_type("PE_NIGHT_PERSON",      PE_NIGHT_PERSON));
  perks_map.insert(params_map::value_type("PE_SKILLED",       PE_SKILLED));
  perks_map.insert(params_map::value_type("PE_GIFTED",        PE_GIFTED));
  perks_map.insert(params_map::value_type("PE_AWARENESS",       PE_AWARENESS));
  perks_map.insert(params_map::value_type("PE_A_MELEE_ATT",     PE_A_MELEE_ATT));
  perks_map.insert(params_map::value_type("PE_A_MELEE_DAM",     PE_A_MELEE_DAM));
  perks_map.insert(params_map::value_type("PE_A_MOVE",        PE_A_MOVE));
  perks_map.insert(params_map::value_type("PE_A_DAM",         PE_A_DAM));
  perks_map.insert(params_map::value_type("PE_A_SPEED",       PE_A_SPEED));
  perks_map.insert(params_map::value_type("PE_PASS_FRONT",      PE_PASS_FRONT));
  perks_map.insert(params_map::value_type("PE_RAPID_HEAL",      PE_RAPID_HEAL));
  perks_map.insert(params_map::value_type("PE_MORE_CRIT_DAM",     PE_MORE_CRIT_DAM));
  perks_map.insert(params_map::value_type("PE_NIGHT_SIGHT",     PE_NIGHT_SIGHT));
  perks_map.insert(params_map::value_type("PE_PRESENCE",        PE_PRESENCE));
  perks_map.insert(params_map::value_type("PE_RES_NUKLEAR",     PE_RES_NUKLEAR));
  perks_map.insert(params_map::value_type("PE_ENDURENCE",       PE_ENDURENCE));
  perks_map.insert(params_map::value_type("PE_STR_BACK",        PE_STR_BACK));
  perks_map.insert(params_map::value_type("PE_MARKSMAN",        PE_MARKSMAN));
  perks_map.insert(params_map::value_type("PE_STEALHING",       PE_STEALHING));
  perks_map.insert(params_map::value_type("PE_LIFEFULL",        PE_LIFEFULL));
  perks_map.insert(params_map::value_type("PE_MERCHANT",        PE_MERCHANT));
  perks_map.insert(params_map::value_type("PE_FORMED",        PE_FORMED));
  perks_map.insert(params_map::value_type("PE_HEALER",        PE_HEALER));
  perks_map.insert(params_map::value_type("PE_TR_DIGGER",       PE_TR_DIGGER));
  perks_map.insert(params_map::value_type("PE_BEST_HITS",       PE_BEST_HITS));
  perks_map.insert(params_map::value_type("PE_COMPASION",       PE_COMPASION));
  perks_map.insert(params_map::value_type("PE_KILLER",        PE_KILLER));
  perks_map.insert(params_map::value_type("PE_SNIPER",        PE_SNIPER));
  perks_map.insert(params_map::value_type("PE_SILENT_DEATH",      PE_SILENT_DEATH));
  perks_map.insert(params_map::value_type("PE_C_FIGHTER",       PE_C_FIGHTER));
  perks_map.insert(params_map::value_type("PE_MIND_BLOCK",      PE_MIND_BLOCK));
  perks_map.insert(params_map::value_type("PE_PROLONGATION_LIFE",   PE_PROLONGATION_LIFE));
  perks_map.insert(params_map::value_type("PE_RECOURCEFULNESS",   PE_RECOURCEFULNESS));
  perks_map.insert(params_map::value_type("PE_SNAKE_EATER",     PE_SNAKE_EATER));
  perks_map.insert(params_map::value_type("PE_REPEARER",        PE_REPEARER));
  perks_map.insert(params_map::value_type("PE_MEDIC",         PE_MEDIC));
  perks_map.insert(params_map::value_type("PE_SKILLED_THIEF",     PE_SKILLED_THIEF));
  perks_map.insert(params_map::value_type("PE_SPEAKER",       PE_SPEAKER));
  perks_map.insert(params_map::value_type("PE_GUTCHER",       PE_GUTCHER));
  perks_map.insert(params_map::value_type("PE_UNKNOWN_1",       PE_UNKNOWN_1));
  perks_map.insert(params_map::value_type("PE_PICK_POCKER",     PE_PICK_POCKER));
  perks_map.insert(params_map::value_type("PE_GHOST",         PE_GHOST));
  perks_map.insert(params_map::value_type("PE_CHAR_CULT",       PE_CHAR_CULT));
  perks_map.insert(params_map::value_type("PE_THIFER",        PE_THIFER));
  perks_map.insert(params_map::value_type("PE_DISCOVER",        PE_DISCOVER));
  perks_map.insert(params_map::value_type("PE_OVERROAD",        PE_OVERROAD));
  perks_map.insert(params_map::value_type("PE_ANIMAL_FRIENDSHIP",   PE_ANIMAL_FRIENDSHIP));
  perks_map.insert(params_map::value_type("PE_SCOUT",         PE_SCOUT));
  perks_map.insert(params_map::value_type("PE_MIST_CHAR",       PE_MIST_CHAR));
  perks_map.insert(params_map::value_type("PE_RANGER",        PE_RANGER));
  perks_map.insert(params_map::value_type("PE_PICK_POCKET_2",     PE_PICK_POCKET_2));
  perks_map.insert(params_map::value_type("PE_INTERLOCUTER",      PE_INTERLOCUTER));
  perks_map.insert(params_map::value_type("PE_NOVICE",        PE_NOVICE));
  perks_map.insert(params_map::value_type("PE_PRIME_SKILL",     PE_PRIME_SKILL));
  perks_map.insert(params_map::value_type("PE_MUTATION",        PE_MUTATION));
  perks_map.insert(params_map::value_type("PE_NARC_NUKACOLA",     PE_NARC_NUKACOLA));
  perks_map.insert(params_map::value_type("PE_NARC_BUFFOUT",      PE_NARC_BUFFOUT));
  perks_map.insert(params_map::value_type("PE_NARC_MENTAT",     PE_NARC_MENTAT));
  perks_map.insert(params_map::value_type("PE_NARC_PSYHO",      PE_NARC_PSYHO));
  perks_map.insert(params_map::value_type("PE_NARC_RADAWAY",      PE_NARC_RADAWAY));
  perks_map.insert(params_map::value_type("PE_DISTANT_WEAP",      PE_DISTANT_WEAP));
  perks_map.insert(params_map::value_type("PE_ACCURARY_WEAP",     PE_ACCURARY_WEAP));
  perks_map.insert(params_map::value_type("PE_PENETRATION_WEAP",    PE_PENETRATION_WEAP));
  perks_map.insert(params_map::value_type("PE_KILLER_WEAP",     PE_KILLER_WEAP));
  perks_map.insert(params_map::value_type("PE_ENERGY_ARMOR",      PE_ENERGY_ARMOR));
  perks_map.insert(params_map::value_type("PE_BATTLE_ARMOR",      PE_BATTLE_ARMOR));
  perks_map.insert(params_map::value_type("PE_WEAP_RANGE",      PE_WEAP_RANGE));
  perks_map.insert(params_map::value_type("PE_RAPID_RELOAD",      PE_RAPID_RELOAD));
  perks_map.insert(params_map::value_type("PE_NIGHT_SPYGLASS",    PE_NIGHT_SPYGLASS));
  perks_map.insert(params_map::value_type("PE_FLAMER",        PE_FLAMER));
  perks_map.insert(params_map::value_type("PE_APA_I",         PE_APA_I));
  perks_map.insert(params_map::value_type("PE_APA_II",        PE_APA_II));
  perks_map.insert(params_map::value_type("PE_FORCEAGE",        PE_FORCEAGE));
  perks_map.insert(params_map::value_type("PE_DEADLY_NARC",     PE_DEADLY_NARC));
  perks_map.insert(params_map::value_type("PE_CHARMOLEANCE",      PE_CHARMOLEANCE));
  perks_map.insert(params_map::value_type("PE_GEKK_SKINER",     PE_GEKK_SKINER));
  perks_map.insert(params_map::value_type("PE_SKIN_ARMOR",      PE_SKIN_ARMOR));
  perks_map.insert(params_map::value_type("PE_A_SKIN_ARMOR",      PE_A_SKIN_ARMOR));
  perks_map.insert(params_map::value_type("PE_SUPER_ARMOR",     PE_SUPER_ARMOR));
  perks_map.insert(params_map::value_type("PE_A_SUPER_ARMOR",     PE_A_SUPER_ARMOR));
  perks_map.insert(params_map::value_type("PE_VAULT_INOCUL",      PE_VAULT_INOCUL));
  perks_map.insert(params_map::value_type("PE_ADRENALIN_RUSH",    PE_ADRENALIN_RUSH));
  perks_map.insert(params_map::value_type("PE_CAREFULL",        PE_CAREFULL));
  perks_map.insert(params_map::value_type("PE_INTELEGENCE",     PE_INTELEGENCE));
  perks_map.insert(params_map::value_type("PE_PYROKASY",        PE_PYROKASY));
  perks_map.insert(params_map::value_type("PE_DUDE",          PE_DUDE));
  perks_map.insert(params_map::value_type("PE_A_STR",         PE_A_STR));
  perks_map.insert(params_map::value_type("PE_A_PER",         PE_A_PER));
  perks_map.insert(params_map::value_type("PE_A_END",         PE_A_END));
  perks_map.insert(params_map::value_type("PE_A_CHA",         PE_A_CHA));
  perks_map.insert(params_map::value_type("PE_A_INT",         PE_A_INT));
  perks_map.insert(params_map::value_type("PE_A_AGL",         PE_A_AGL));
  perks_map.insert(params_map::value_type("PE_A_LUC",         PE_A_LUC));
  perks_map.insert(params_map::value_type("PE_PURERER",       PE_PURERER));
  perks_map.insert(params_map::value_type("PE_IMAG",          PE_IMAG));
  perks_map.insert(params_map::value_type("PE_EVASION",       PE_EVASION));
  perks_map.insert(params_map::value_type("PE_DROSHKADRAT",     PE_DROSHKADRAT));
  perks_map.insert(params_map::value_type("PE_KARMA_GLOW",      PE_KARMA_GLOW));
  perks_map.insert(params_map::value_type("PE_SILENT_STEPS",      PE_SILENT_STEPS));
  perks_map.insert(params_map::value_type("PE_ANATOMY",       PE_ANATOMY));
  perks_map.insert(params_map::value_type("PE_CHAMER",        PE_CHAMER));
  perks_map.insert(params_map::value_type("PE_ORATOR",        PE_ORATOR));
  perks_map.insert(params_map::value_type("PE_PACKER",        PE_PACKER));
  perks_map.insert(params_map::value_type("PE_EDD_GAYAN_MANIAC",    PE_EDD_GAYAN_MANIAC));
  perks_map.insert(params_map::value_type("PE_FAST_REGENERATION",   PE_FAST_REGENERATION));
  perks_map.insert(params_map::value_type("PE_VENDOR",        PE_VENDOR));
  perks_map.insert(params_map::value_type("PE_STONE_WALL",      PE_STONE_WALL));
  perks_map.insert(params_map::value_type("PE_THIEF_AGAIN",     PE_THIEF_AGAIN));
  perks_map.insert(params_map::value_type("PE_WEAPON_SKILL",      PE_WEAPON_SKILL));
  perks_map.insert(params_map::value_type("PE_MAKE_VAULT",      PE_MAKE_VAULT));
  perks_map.insert(params_map::value_type("PE_ALC_BUFF_1",      PE_ALC_BUFF_1));
  perks_map.insert(params_map::value_type("PE_ALC_BUFF_2",      PE_ALC_BUFF_2));
/*  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  perks_map.insert(params_map::value_type("!",!));
  */
  perks_map.insert(params_map::value_type("PE_HIDE_MODE",       PE_HIDE_MODE));

  object_map.insert(params_map::value_type("OBJ_TYPE_ARMOR",      OBJ_TYPE_ARMOR));
  object_map.insert(params_map::value_type("OBJ_TYPE_CONTAINER",    OBJ_TYPE_CONTAINER));
  object_map.insert(params_map::value_type("OBJ_TYPE_DRUG",     OBJ_TYPE_DRUG));
  object_map.insert(params_map::value_type("OBJ_TYPE_WEAPON",     OBJ_TYPE_WEAPON));
  object_map.insert(params_map::value_type("OBJ_TYPE_AMMO",     OBJ_TYPE_AMMO));
  object_map.insert(params_map::value_type("OBJ_TYPE_MISC",     OBJ_TYPE_MISC));
  object_map.insert(params_map::value_type("OBJ_TYPE_KEY",      OBJ_TYPE_KEY));
  object_map.insert(params_map::value_type("OBJ_TYPE_DOOR",     OBJ_TYPE_DOOR));
  object_map.insert(params_map::value_type("OBJ_NAME",        OBJ_NAME));
  object_map.insert(params_map::value_type("OBJ_INFO",        OBJ_INFO));
  object_map.insert(params_map::value_type("OBJ_TIME_SHOW",     OBJ_TIME_SHOW));
  object_map.insert(params_map::value_type("OBJ_TIME_HIDE",     OBJ_TIME_HIDE));
  object_map.insert(params_map::value_type("OBJ_DISTANCE_LIGHT",    OBJ_DISTANCE_LIGHT));
  object_map.insert(params_map::value_type("OBJ_INTENSITY_LIGHT",   OBJ_INTENSITY_LIGHT));
  object_map.insert(params_map::value_type("OBJ_PASSED",        OBJ_PASSED));
  object_map.insert(params_map::value_type("OBJ_RAKED",       OBJ_RAKED));
  object_map.insert(params_map::value_type("OBJ_TRANSPARENT",     OBJ_TRANSPARENT));
  object_map.insert(params_map::value_type("OBJ_CAN_USE",       OBJ_CAN_USE));
  object_map.insert(params_map::value_type("OBJ_CAN_PICK_UP",     OBJ_CAN_PICK_UP));
  object_map.insert(params_map::value_type("OBJ_CAN_USE_ON_SMTH",   OBJ_CAN_USE_ON_SMTH));
  object_map.insert(params_map::value_type("OBJ_HIDDEN",        OBJ_HIDDEN));
  object_map.insert(params_map::value_type("OBJ_WEIGHT",        OBJ_WEIGHT));
  object_map.insert(params_map::value_type("OBJ_SIZE",        OBJ_SIZE));
  object_map.insert(params_map::value_type("OBJ_TWOHANDS",      OBJ_TWOHANDS));
  object_map.insert(params_map::value_type("OBJ_PIC_MAP",       OBJ_PIC_MAP));
  object_map.insert(params_map::value_type("OBJ_ANIM_ON_MAP",     OBJ_ANIM_ON_MAP));
  object_map.insert(params_map::value_type("OBJ_PIC_INV",       OBJ_PIC_INV));
  object_map.insert(params_map::value_type("OBJ_SOUND",       OBJ_SOUND));
  object_map.insert(params_map::value_type("OBJ_LIVETIME",      OBJ_LIVETIME));
  object_map.insert(params_map::value_type("OBJ_COST",        OBJ_COST));
  object_map.insert(params_map::value_type("OBJ_MATERIAL",      OBJ_MATERIAL));
  object_map.insert(params_map::value_type("OBJ_ARM_ANIM0_MALE",    OBJ_ARM_ANIM0_MALE));
  object_map.insert(params_map::value_type("OBJ_ARM_ANIM0_MALE2",   OBJ_ARM_ANIM0_MALE2));
  object_map.insert(params_map::value_type("OBJ_ARM_ANIM0_FEMALE",  OBJ_ARM_ANIM0_FEMALE));
  object_map.insert(params_map::value_type("OBJ_ARM_ANIM0_FEMALE2", OBJ_ARM_ANIM0_FEMALE2));
  object_map.insert(params_map::value_type("OBJ_ARM_AC",        OBJ_ARM_AC));
  object_map.insert(params_map::value_type("OBJ_ARM_PERK",      OBJ_ARM_PERK));
  object_map.insert(params_map::value_type("OBJ_ARM_DR_NORMAL",   OBJ_ARM_DR_NORMAL));
  object_map.insert(params_map::value_type("OBJ_ARM_DR_LASER",    OBJ_ARM_DR_LASER));
  object_map.insert(params_map::value_type("OBJ_ARM_DR_FIRE",     OBJ_ARM_DR_FIRE));
  object_map.insert(params_map::value_type("OBJ_ARM_DR_PLASMA",   OBJ_ARM_DR_PLASMA));
  object_map.insert(params_map::value_type("OBJ_ARM_DR_ELECTR",   OBJ_ARM_DR_ELECTR));
  object_map.insert(params_map::value_type("OBJ_ARM_DR_EMP",      OBJ_ARM_DR_EMP));
  object_map.insert(params_map::value_type("OBJ_ARM_DR_EXPLODE",    OBJ_ARM_DR_EXPLODE));
  object_map.insert(params_map::value_type("OBJ_ARM_DT_NORMAL",   OBJ_ARM_DT_NORMAL));
  object_map.insert(params_map::value_type("OBJ_ARM_DT_LASER",    OBJ_ARM_DT_LASER));
  object_map.insert(params_map::value_type("OBJ_ARM_DT_FIRE",     OBJ_ARM_DT_FIRE));
  object_map.insert(params_map::value_type("OBJ_ARM_DT_PLASMA",   OBJ_ARM_DT_PLASMA));
  object_map.insert(params_map::value_type("OBJ_ARM_DT_ELECTR",   OBJ_ARM_DT_ELECTR));
  object_map.insert(params_map::value_type("OBJ_ARM_DT_EMP",      OBJ_ARM_DT_EMP));
  object_map.insert(params_map::value_type("OBJ_ARM_DT_EXPLODE",    OBJ_ARM_DT_EXPLODE));
  object_map.insert(params_map::value_type("OBJ_CONT_SIZE",     OBJ_CONT_SIZE));
  object_map.insert(params_map::value_type("OBJ_CONT_FLAG",     OBJ_CONT_FLAG));
  object_map.insert(params_map::value_type("OBJ_DRUG_STAT0",      OBJ_DRUG_STAT0));
  object_map.insert(params_map::value_type("OBJ_DRUG_STAT1",      OBJ_DRUG_STAT1));
  object_map.insert(params_map::value_type("OBJ_DRUG_STAT2",      OBJ_DRUG_STAT2));
  object_map.insert(params_map::value_type("OBJ_DRUG_AMOUNT0_S0",   OBJ_DRUG_AMOUNT0_S0));
  object_map.insert(params_map::value_type("OBJ_DRUG_AMOUNT0_S1",   OBJ_DRUG_AMOUNT0_S1));
  object_map.insert(params_map::value_type("OBJ_DRUG_AMOUNT0_S2",   OBJ_DRUG_AMOUNT0_S2));
  object_map.insert(params_map::value_type("OBJ_DRUG_DURATION1",    OBJ_DRUG_DURATION1));
  object_map.insert(params_map::value_type("OBJ_DRUG_AMOUNT1_S0",   OBJ_DRUG_AMOUNT1_S0));
  object_map.insert(params_map::value_type("OBJ_DRUG_AMOUNT1_S1",   OBJ_DRUG_AMOUNT1_S1));
  object_map.insert(params_map::value_type("OBJ_DRUG_AMOUNT1_S2",   OBJ_DRUG_AMOUNT1_S2));
  object_map.insert(params_map::value_type("OBJ_DRUG_DURATION2",    OBJ_DRUG_DURATION2));
  object_map.insert(params_map::value_type("OBJ_DRUG_AMOUNT2_S0",   OBJ_DRUG_AMOUNT2_S0));
  object_map.insert(params_map::value_type("OBJ_DRUG_AMOUNT2_S1",   OBJ_DRUG_AMOUNT2_S1));
  object_map.insert(params_map::value_type("OBJ_DRUG_AMOUNT2_S2",   OBJ_DRUG_AMOUNT2_S2));
  object_map.insert(params_map::value_type("OBJ_DRUG_ADDICTION",    OBJ_DRUG_ADDICTION));
  object_map.insert(params_map::value_type("OBJ_DRUG_W_EFFECT",   OBJ_DRUG_W_EFFECT));
  object_map.insert(params_map::value_type("OBJ_DRUG_W_ONSET",    OBJ_DRUG_W_ONSET));
  object_map.insert(params_map::value_type("OBJ_WEAP_ANIM1",      OBJ_WEAP_ANIM1));
  object_map.insert(params_map::value_type("OBJ_WEAP_TIME_ACTIV",   OBJ_WEAP_TIME_ACTIV));
  object_map.insert(params_map::value_type("OBJ_WEAP_TIME_UNACTIV", OBJ_WEAP_TIME_UNACTIV));
  object_map.insert(params_map::value_type("OBJ_WEAP_VOL_HOLDER",   OBJ_WEAP_VOL_HOLDER));
  object_map.insert(params_map::value_type("OBJ_WEAP_CALIBER",    OBJ_WEAP_CALIBER));
  object_map.insert(params_map::value_type("OBJ_WEAP_VOL_HOLDER_E", OBJ_WEAP_VOL_HOLDER_E));
  object_map.insert(params_map::value_type("OBJ_WEAP_CALIBER_E",    OBJ_WEAP_CALIBER_E));
  object_map.insert(params_map::value_type("OBJ_WEAP_CR_FAILTURE",  OBJ_WEAP_CR_FAILTURE));
  object_map.insert(params_map::value_type("OBJ_WEAP_TYPE_ATTACK",  OBJ_WEAP_TYPE_ATTACK));
  object_map.insert(params_map::value_type("OBJ_WEAP_COUNT_ATTACK", OBJ_WEAP_COUNT_ATTACK));
  object_map.insert(params_map::value_type("OBJ_WEAP_PA_SKILL",   OBJ_WEAP_PA_SKILL));
  object_map.insert(params_map::value_type("OBJ_WEAP_PA_HOLDER",    OBJ_WEAP_PA_HOLDER));
  object_map.insert(params_map::value_type("OBJ_WEAP_PA_PIC",     OBJ_WEAP_PA_PIC));
  object_map.insert(params_map::value_type("OBJ_WEAP_PA_DMG_MIN",   OBJ_WEAP_PA_DMG_MIN));
  object_map.insert(params_map::value_type("OBJ_WEAP_PA_DMG_MAX",   OBJ_WEAP_PA_DMG_MAX));
  object_map.insert(params_map::value_type("OBJ_WEAP_PA_MAX_DIST",  OBJ_WEAP_PA_MAX_DIST));
  object_map.insert(params_map::value_type("OBJ_WEAP_PA_EFF_DIST",  OBJ_WEAP_PA_EFF_DIST));
  object_map.insert(params_map::value_type("OBJ_WEAP_PA_ANIM2",   OBJ_WEAP_PA_ANIM2));
  object_map.insert(params_map::value_type("OBJ_WEAP_PA_TIME",    OBJ_WEAP_PA_TIME));
  object_map.insert(params_map::value_type("OBJ_WEAP_PA_AIM",     OBJ_WEAP_PA_AIM));
  object_map.insert(params_map::value_type("OBJ_WEAP_PA_ROUND",   OBJ_WEAP_PA_ROUND));
  object_map.insert(params_map::value_type("OBJ_WEAP_PA_REMOVE",    OBJ_WEAP_PA_REMOVE));
  object_map.insert(params_map::value_type("OBJ_WEAP_SA_SKILL",   OBJ_WEAP_SA_SKILL));
  object_map.insert(params_map::value_type("OBJ_WEAP_SA_HOLDER",    OBJ_WEAP_SA_HOLDER));
  object_map.insert(params_map::value_type("OBJ_WEAP_SA_PIC",     OBJ_WEAP_SA_PIC));
  object_map.insert(params_map::value_type("OBJ_WEAP_SA_DMG_MIN",   OBJ_WEAP_SA_DMG_MIN));
  object_map.insert(params_map::value_type("OBJ_WEAP_SA_DMG_MAX",   OBJ_WEAP_SA_DMG_MAX));
  object_map.insert(params_map::value_type("OBJ_WEAP_SA_MAX_DIST",  OBJ_WEAP_SA_MAX_DIST));
  object_map.insert(params_map::value_type("OBJ_WEAP_SA_EFF_DIST",  OBJ_WEAP_SA_EFF_DIST));
  object_map.insert(params_map::value_type("OBJ_WEAP_SA_ANIM2",   OBJ_WEAP_SA_ANIM2));
  object_map.insert(params_map::value_type("OBJ_WEAP_SA_TIME",    OBJ_WEAP_SA_TIME));
  object_map.insert(params_map::value_type("OBJ_WEAP_SA_AIM",     OBJ_WEAP_SA_AIM));
  object_map.insert(params_map::value_type("OBJ_WEAP_SA_ROUND",   OBJ_WEAP_SA_ROUND));
  object_map.insert(params_map::value_type("OBJ_WEAP_SA_REMOVE",    OBJ_WEAP_SA_REMOVE));
  object_map.insert(params_map::value_type("OBJ_WEAP_TA_SKILL",   OBJ_WEAP_TA_SKILL));
  object_map.insert(params_map::value_type("OBJ_WEAP_TA_HOLDER",    OBJ_WEAP_TA_HOLDER));
  object_map.insert(params_map::value_type("OBJ_WEAP_TA_PIC",     OBJ_WEAP_TA_PIC));
  object_map.insert(params_map::value_type("OBJ_WEAP_TA_DMG_MIN",   OBJ_WEAP_TA_DMG_MIN));
  object_map.insert(params_map::value_type("OBJ_WEAP_TA_DMG_MAX",   OBJ_WEAP_TA_DMG_MAX));
  object_map.insert(params_map::value_type("OBJ_WEAP_TA_MAX_DIST",  OBJ_WEAP_TA_MAX_DIST));
  object_map.insert(params_map::value_type("OBJ_WEAP_TA_EFF_DIST",  OBJ_WEAP_TA_EFF_DIST));
  object_map.insert(params_map::value_type("OBJ_WEAP_TA_ANIM2",   OBJ_WEAP_TA_ANIM2));
  object_map.insert(params_map::value_type("OBJ_WEAP_TA_TIME",    OBJ_WEAP_TA_TIME));
  object_map.insert(params_map::value_type("OBJ_WEAP_TA_AIM",     OBJ_WEAP_TA_AIM));
  object_map.insert(params_map::value_type("OBJ_WEAP_TA_ROUND",   OBJ_WEAP_TA_ROUND));
  object_map.insert(params_map::value_type("OBJ_WEAP_TA_REMOVE",    OBJ_WEAP_TA_REMOVE));
  object_map.insert(params_map::value_type("OBJ_AMMO_CALIBER",    OBJ_AMMO_CALIBER));
  object_map.insert(params_map::value_type("OBJ_AMMO_TYPE_DAMAGE",  OBJ_AMMO_TYPE_DAMAGE));
  object_map.insert(params_map::value_type("OBJ_AMMO_QUANTITY",   OBJ_AMMO_QUANTITY));
  object_map.insert(params_map::value_type("OBJ_AMMO_AC",       OBJ_AMMO_AC));
  object_map.insert(params_map::value_type("OBJ_AMMO_DR",       OBJ_AMMO_DR));
  object_map.insert(params_map::value_type("OBJ_AMMO_DM",       OBJ_AMMO_DM));
  object_map.insert(params_map::value_type("OBJ_AMMO_DD",       OBJ_AMMO_DD));
}

void FOnlineEngine::DoLost()
{
  cmn_lost=1;
  islost=1;
  dilost=1;

  if(opt_fullscr) //!!!!!??????
  {
    D3DPRESENT_PARAMETERS d3dpp;

    ZeroMemory(&d3dpp,sizeof(d3dpp));
    d3dpp.Windowed=1;

    lpDevice->Reset(&d3dpp);
  }
}

void FOnlineEngine::TryExit()
{
  if(state==STATE_DISCONNECT)
  {
    FONLINE_LOG("Quit user on ESCAPE\n");
    DestroyWindow(hWnd);
  }
  NetDiscon();
}

void FOnlineEngine::GetRandomSplash(char* splash_name)
{
  char path_splash[128];
  strcpy(path_splash, ".\\data\\");
  strcat(path_splash, pathlst[PT_ART_SPLASH]);
  strcat(path_splash, "params.txt");

  int splash_count = GetPrivateProfileInt("splash", "count", 0, path_splash);
  int cur_splash = rand() % splash_count;

  char itoc[16];
  sprintf(itoc, "%d", cur_splash);

  GetPrivateProfileString("splash", itoc, "splash0.rix", splash_name, 63, path_splash);
}
// !Cvet ----------------------------------------------------------------
