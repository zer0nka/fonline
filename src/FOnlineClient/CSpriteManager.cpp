#include "stdafx.h"

#include "CSpriteManager.h"
#include "common.h"

#include <IniFile/IniFile.hpp>

#define MODE_WIDTH (screen_width[opt_screen_mode]) //!Cvet
#define MODE_HEIGHT (screen_height[opt_screen_mode]) //!Cvet

//#include <SimpleLeakDetector/SimpleLeakDetector.hpp>
/********************************************************************
  created:  2005   22:04
  edit:   2007   15:15

  author:   Oleg Mareskin
  edit:   Denis Balikhin, Anton Tsvetinsky

  purpose:
*********************************************************************/

//#define TEX_FRMT D3DFMT_A1R5G5B5
//#define TEX_FRMT D3DFMT_A4R4G4B4
#define TEX_FRMT D3DFMT_A8R8G8B8

#include "frmload.h"
//-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-//

CSpriteManager::CSpriteManager(): initialized(0), maxSpriteCount(0), currentPosition(0),
  lpDevice(NULL),lpVB(NULL),lpIB(NULL),lpWaitBuf(NULL),lastSurface(NULL),next_id(1),currentSurface(NULL)
{
  col=D3DCOLOR_ARGB(255,128,128,128);

  memset(this->CrAnim, 0, sizeof(CrAnim));
}

int CSpriteManager::Init(LPDIRECT3DDEVICE8 lpD3Device) {
  assert(!initialized);
  assert(lpD3Device != NULL);

  FONLINE_LOG("CSpriteManager Initialization...\n");

  maxSpriteCount = opt_flushval;
  currentPosition = 0;

  lpDevice = lpD3Device;

  //Создаем буфер вершин
  FONLINE_LOG("Создаю VB на %d спрайтов\n",maxSpriteCount);
  HRESULT hr=lpDevice->CreateVertexBuffer(maxSpriteCount*4*sizeof(Vertex),D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC,
    D3DFVF_VERTEX_FORMAT,D3DPOOL_DEFAULT,&lpVB);
  if(hr!=D3D_OK){
    ReportErrorMessage("SM::CreateVertexBuffer",(char*)DXGetErrorString8(hr));
    return 0;
  }

  //и индексов
  hr=lpDevice->CreateIndexBuffer(maxSpriteCount*6*sizeof(uint16_t),D3DUSAGE_WRITEONLY,
    D3DFMT_INDEX16,D3DPOOL_DEFAULT,&lpIB);
  if(hr!=D3D_OK){
    ReportErrorMessage("SM::CreateIndexBuffer",(char*)DXGetErrorString8(hr));
    return 0;
  }


  uint16_t* IndexList=new uint16_t[6*maxSpriteCount];
  for(int i=0;i<maxSpriteCount;i++)
  {
    IndexList[6*i+0]=4*i+0;
    IndexList[6*i+1]=4*i+1;
    IndexList[6*i+2]=4*i+3;
    IndexList[6*i+3]=4*i+1;
    IndexList[6*i+4]=4*i+2;
    IndexList[6*i+5]=4*i+3;
  }

  void* pBuffer;
  lpIB->Lock(0,0,(uint8_t**)&pBuffer,0);
    memcpy(pBuffer,IndexList,maxSpriteCount*6*sizeof(uint16_t));
  lpIB->Unlock();

  delete[] IndexList;

  lpDevice->SetIndices(lpIB,0);
  lpDevice->SetStreamSource(0,lpVB,sizeof(Vertex));
  lpDevice->SetVertexShader(D3DFVF_VERTEX_FORMAT);


  lpWaitBuf=new Vertex[maxSpriteCount*4];

  if(!fm.Init(opt_masterpath.c_str(), opt_critterpath.c_str(), opt_fopath.c_str())) return 0;

  if(!LoadCritterTypes()) return 0; //!Cvet

  FONLINE_LOG("CSpriteManager Initialization complete\n");
  initialized=1;
  return 1;
}

void CSpriteManager::Clear() {
  assert(initialized);

  FONLINE_LOG("Clearing all sprites.\n");

  fm.Clear();

  for(surf_vect::iterator it=surfaceList.begin();it!=surfaceList.end();it++)
    (*it)->Release();
  surfaceList.clear();

  for(sprinfo_map::iterator ii=spriteData.begin();ii!=spriteData.end();ii++)
    delete (*ii).second;
  spriteData.clear();

  for(onesurf_vec::iterator iv=call_vec.begin();iv!=call_vec.end();iv++)
    delete (*iv);
  call_vec.clear();

  if (lpVB != NULL) {
    lpVB->Release();
    lpVB = NULL;
  }
  if (lpIB != NULL) {
    lpIB->Release();
    lpIB = NULL;
  }
  if (lpWaitBuf != NULL) {
    delete [] lpWaitBuf;
    lpWaitBuf = NULL;
  }

  initialized=0;
  FONLINE_LOG("Clearing complete.\n");
}

//-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-//
int CSpriteManager::LoadMiniSprite(char *fname,double size,int PathType,SpriteInfo** ppInfo) {
  assert(initialized);
  assert(fname != NULL);
  assert(fname[0] != '\0');
  //assert(ppInfo != NULL);

  if(!initialized) return 0;
  if(!fname[0]) return 0;

  if(!fm.LoadFile(fname,PathType))
    return 0;

  SpriteInfo* lpinf=new SpriteInfo;

  short offs_x, offs_y;
  fm.SetCurrentPosition(0xA);
  offs_x=fm.GetWord();
  fm.SetCurrentPosition(0x16);
  offs_y=fm.GetWord();

  lpinf->offsetX=offs_x;
  lpinf->offsetY=offs_y;


  fm.SetCurrentPosition(0x3e);
  uint16_t w=fm.GetWord();
  uint16_t h=fm.GetWord();
  lpinf->width=w;
  lpinf->height=h;
  if(!lastSurface)
  {
    lpinf->surface=CreateNewSurface(w,h);
    if(!lpinf->surface) return 0;
    lpinf->spriteRect(0,0,w,h);
    lastSurface=lpinf->surface;
  }
  else if( (last_w-free_x)>=w && (last_h-free_y)>=h )
  {
    lpinf->surface=lastSurface;
    lpinf->spriteRect(free_x,free_y,free_x+w,free_y+h);
  }
  else if( last_w>=w && (last_h-busy_h)>=h )
  {
    free_x=0;
    free_y=busy_h;
    lpinf->surface=lastSurface;
    lpinf->spriteRect(free_x,free_y,free_x+w,free_y+h);
  }
  else
  {
    lpinf->surface=CreateNewSurface(w,h);
    if(!lpinf->surface) return 0;
    lpinf->spriteRect(0,0,w,h);
    lastSurface=lpinf->surface;
  }

  int aligned_width = (4 - w%4)%4;

  // let's write the bmp
  uint32_t wpos=sizeof(bmpheader);
  uint8_t* res=new uint8_t[wpos+h*(w+aligned_width)];
  memcpy(res,bmpheader,wpos);


  uint32_t ptr = 0x4A+w*(h-1);
  for(int i=0;i < h; i++)
  {
    fm.SetCurrentPosition(ptr);
    fm.Read(res+wpos,w);
    wpos+=w;
    memset(res+wpos,0,aligned_width);
    wpos+=aligned_width;
    ptr -= w;
  }

  uint32_t* spos=(uint32_t*)(res+18);
  spos[0]=w;
  spos[1]=h;


  LPDIRECT3DSURFACE8 lpsurf;
  LPDIRECT3DSURFACE8 lptexsurf;
  //HRESULT hr=lpDevice->CreateImageSurface(w,h,D3DFMT_A8R8G8B8,&lpsurf);
  HRESULT hr=lpDevice->CreateImageSurface(w,h,TEX_FRMT,&lpsurf);
  if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteManager LoadSprite","Не могу создать Surface для файла %s",fname);
    return 0;
  }

  lastSurface->GetSurfaceLevel(0,&lptexsurf);

  hr=D3DXLoadSurfaceFromFileInMemory(lpsurf,NULL,NULL,res,wpos,NULL,D3DX_FILTER_NONE,D3DCOLOR_ARGB(255,0,0,0),NULL);
  if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteManager LoadSprite","Не могу загрузить Surface из памяти для файла %s",fname);
    return 0;
  }

  POINT p={free_x,free_y};
  RECT r={0,0,w,h};

  lpDevice->CopyRects(lpsurf,&r,1,lptexsurf,&p);
  if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteManager LoadSprite","Ошибка при копировании поверхностей для файла %s",fname);
    return 0;
  }

  lpsurf->Release();
  lptexsurf->Release();

  delete[] res;

  free_x+=w;
  if((free_y+h)>busy_h) busy_h=free_y+h;
  lpinf->spriteRect.x1/=last_w;
  lpinf->spriteRect.x2/=last_w;
  lpinf->spriteRect.y1/=last_h;
  lpinf->spriteRect.y2/=last_h;
  lpinf->height/=size; // Уменьшаем пропорции предметов для инвентаря
  lpinf->width/=size; // Статическое масштабирование

  FONLINE_LOG("size %d",size);

  spriteData[next_id++]=lpinf;

  fm.UnloadFile();

  if(ppInfo) (*ppInfo)=lpinf;

  return next_id-1;
}

int CSpriteManager::LoadSprite(char *fname,int PathType,SpriteInfo** ppInfo) //!Cvet переименовал
{
  assert(initialized);
  assert(fname != NULL);
  assert(fname[0] != '\0');
  //assert(ppInfo != NULL);

  if(!initialized) return 0;
  if(!fname[0]) return 0;

  if(!fm.LoadFile(fname,PathType))
    return 0;

  char* ext=strstr(fname,".");

  if(!ext)
  {
    fm.UnloadFile();
    FONLINE_LOG("Нет расширения у файла:|%s|\n",fname);
    return 0;
  }

  if(stricmp(ext,".frm")) return LoadSpriteAlt(fname,PathType);

  SpriteInfo* lpinf=new SpriteInfo;

  short offs_x, offs_y;
  fm.SetCurrentPosition(0xA);
  offs_x=fm.GetWord();
  fm.SetCurrentPosition(0x16);
  offs_y=fm.GetWord();

  lpinf->offsetX=offs_x;
  lpinf->offsetY=offs_y;


  fm.SetCurrentPosition(0x3e);
  uint16_t w=fm.GetWord();
  uint16_t h=fm.GetWord();
  lpinf->width=w;
  lpinf->height=h;
  if(!lastSurface)
  {
    lpinf->surface=CreateNewSurface(w,h);
    if(!lpinf->surface) return 0;
    lpinf->spriteRect(0,0,w,h);
    lastSurface=lpinf->surface;
  }
  else if( (last_w-free_x)>=w && (last_h-free_y)>=h )
  {
    lpinf->surface=lastSurface;
    lpinf->spriteRect(free_x,free_y,free_x+w,free_y+h);
  }
  else if( last_w>=w && (last_h-busy_h)>=h )
  {
    free_x=0;
    free_y=busy_h;
    lpinf->surface=lastSurface;
    lpinf->spriteRect(free_x,free_y,free_x+w,free_y+h);
  }
  else
  {
    lpinf->surface=CreateNewSurface(w,h);
    if(!lpinf->surface) return 0;
    lpinf->spriteRect(0,0,w,h);
    lastSurface=lpinf->surface;
  }

  int aligned_width = (4 - w%4)%4;

  // let's write the bmp
  uint32_t wpos=sizeof(bmpheader);
  uint8_t* res=new uint8_t[wpos+h*(w+aligned_width)];
  memcpy(res,bmpheader,wpos);


  uint32_t ptr = 0x4A+w*(h-1);
  for(int i=0;i < h; i++)
  {
    fm.SetCurrentPosition(ptr);
    fm.Read(res+wpos,w);
    wpos+=w;
    memset(res+wpos,0,aligned_width);
    wpos+=aligned_width;
    ptr -= w;
  }

  uint32_t* spos=(uint32_t*)(res+18);
  spos[0]=w;
  spos[1]=h;


  LPDIRECT3DSURFACE8 lpsurf;
  LPDIRECT3DSURFACE8 lptexsurf;
  //HRESULT hr=lpDevice->CreateImageSurface(w,h,D3DFMT_A8R8G8B8,&lpsurf);
  HRESULT hr=lpDevice->CreateImageSurface(w,h,TEX_FRMT,&lpsurf);
  if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteManager LoadSprite","Не могу создать Surface для файла %s",fname);
    return 0;
  }

  lastSurface->GetSurfaceLevel(0,&lptexsurf);

  hr=D3DXLoadSurfaceFromFileInMemory(lpsurf,NULL,NULL,res,wpos,NULL,D3DX_FILTER_NONE,D3DCOLOR_ARGB(255,0,0,0),NULL);
  if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteManager LoadSprite","Не могу загрузить Surface из памяти для файла %s",fname);
    return 0;
  }

  POINT p={free_x,free_y};
  RECT r={0,0,w,h};

  lpDevice->CopyRects(lpsurf,&r,1,lptexsurf,&p);
  if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteManager LoadSprite","Ошибка при копировании поверхностей для файла %s",fname);
    return 0;
  }

  lpsurf->Release();
  lptexsurf->Release();

  delete[] res;

  free_x+=w;
  if((free_y+h)>busy_h) busy_h=free_y+h;
  lpinf->spriteRect.x1/=last_w;
  lpinf->spriteRect.x2/=last_w;
  lpinf->spriteRect.y1/=last_h;
  lpinf->spriteRect.y2/=last_h;

  spriteData[next_id++]=lpinf;

  fm.UnloadFile();

  if(ppInfo) (*ppInfo)=lpinf;

  return next_id-1;
}

//!Cvet +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
int CSpriteManager::LoadSpriteAlt(char *fname,int PathType,SpriteInfo** ppInfo)
{
  assert(initialized);
  assert(fname != NULL);
  assert(fname[0] != '\0');
  //assert(ppInfo != NULL);

  char* ext=strstr(fname,".");

  if(!ext)
  {
    fm.UnloadFile();
    FONLINE_LOG("Нет расширения у файла:|%s|\n",fname);
    return 0;
  }

  SpriteInfo* lpinf=new SpriteInfo;
  size_t w=0;
  size_t h=0;

//.bmp+, .dds-, .dib-, .hdr-, .jpg+, .pfm-, .png+, .ppm-, .tga-

  if(!stricmp(ext,".bmp"))
  {
    fm.SetCurrentPosition(0x12); //смещение для размеров
    w=fm.GetRDWord(); //была написана обратная функция
    h=fm.GetRDWord();
  }
  else if(!stricmp(ext,".png"))
  {
    fm.SetCurrentPosition(0x10);
    w=fm.GetDWord();
    h=fm.GetDWord();
  }
  else if(!stricmp(ext,".jpg") || !stricmp(ext,".jpeg"))
  {
    fm.SetCurrentPosition(0xBB);
    h=fm.GetWord();
    w=fm.GetWord();
  }
  else
  {
  if (lpinf != NULL) {
    delete lpinf;
    lpinf = NULL;
  }
    fm.UnloadFile();
    FONLINE_LOG("Неизвестный формат файла:|%s|\n",ext);
    return 0;
  }

  fm.UnloadFile();

  lpinf->width=w;
  lpinf->height=h;

  lpinf->offsetX=0;
  lpinf->offsetY=0;

  if(!lastSurface)
  {
    lpinf->surface=CreateNewSurface(w,h);
    if(!lpinf->surface) return 0;
    lpinf->spriteRect(0,0,w,h);
    lastSurface=lpinf->surface;
  }
  else if( (last_w-free_x)>=w && (last_h-free_y)>=h )
  {
    lpinf->surface=lastSurface;
    lpinf->spriteRect(free_x,free_y,free_x+w,free_y+h);
  }
  else if( last_w>=w && (last_h-busy_h)>=h )
  {
    free_x=0;
    free_y=busy_h;
    lpinf->surface=lastSurface;
    lpinf->spriteRect(free_x,free_y,free_x+w,free_y+h);
  }
  else
  {
    lpinf->surface=CreateNewSurface(w,h);
    if(!lpinf->surface) return 0;
    lpinf->spriteRect(0,0,w,h);
    lastSurface=lpinf->surface;
  }

  LPDIRECT3DSURFACE8 lpsurf;
  LPDIRECT3DSURFACE8 lptexsurf;
  //HRESULT hr=lpDevice->CreateImageSurface(w,h,D3DFMT_A8R8G8B8,&lpsurf);
  HRESULT hr=lpDevice->CreateImageSurface(w,h,TEX_FRMT,&lpsurf);
  if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteManager LoadSprite","Не могу создать Surface для файла %s",fname);
    return 0;
  }

  lastSurface->GetSurfaceLevel(0,&lptexsurf);

  char full_path[1024];
  if(!fm.GetFullPath(fname,PathType,full_path)) return 0;

  hr=D3DXLoadSurfaceFromFile(lpsurf,NULL,NULL,full_path,NULL,D3DX_FILTER_NONE,D3DCOLOR_ARGB(255,0,0,255),NULL);
  if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteManager LoadSprite","Не могу загрузить Surface для файла %s",fname);
    return 0;
  }

  POINT p={free_x,free_y};
  RECT r={0,0,w,h};

  lpDevice->CopyRects(lpsurf,&r,1,lptexsurf,&p);
  if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteManager LoadSprite","Ошибка при копировании поверхностей для файла %s",fname);
    return 0;
  }

  lpsurf->Release();
  lptexsurf->Release();

  free_x+=w;
  if((free_y+h)>busy_h) busy_h=free_y+h;
  lpinf->spriteRect.x1/=last_w;
  lpinf->spriteRect.x2/=last_w;
  lpinf->spriteRect.y1/=last_h;
  lpinf->spriteRect.y2/=last_h;

  spriteData[next_id++]=lpinf;

  if(ppInfo) (*ppInfo)=lpinf;

  return next_id-1;
}
//!Cvet -----------------------------------------------------------------------

int CSpriteManager::LoadAnimation(char *fname,int PathType,CritFrames* pframes)
{
  assert(initialized);
  assert(fname != NULL);
  assert(fname[0] != '\0');
  assert(pframes != NULL);

  if(!initialized) return 0;
  if(!fname[0]) return 0;

//  FONLINE_LOG("Loading animation %s\n",fname);

  if(!fm.LoadFile(fname,PathType))
    return 0;

  fm.SetCurrentPosition(0x4); //!Cvet
  uint16_t frm_fps=fm.GetWord(); //!Cvet
  if(!frm_fps) frm_fps=10; //!Cvet

  fm.SetCurrentPosition(0x8);
  uint16_t frm_num=fm.GetWord();

  //Получаем общие смещения по всем направлениям
  short offs_x[6], offs_y[6];
  fm.SetCurrentPosition(0xA);
  for(int i=0;i<6;i++)
    offs_x[i]=fm.GetWord();
  fm.SetCurrentPosition(0x16);
  for(int i=0;i<6;i++)
    offs_y[i]=fm.GetWord();

  pframes->ticks=1000/frm_fps*frm_num; //!Cvet
  pframes->cnt_frames=frm_num;
  pframes->ind=new uint16_t[frm_num*6];
  pframes->next_x=new short[frm_num*6];
  pframes->next_y=new short[frm_num*6];
  for(int i=0;i<6;i++)
    pframes->dir_offs[i]=i*frm_num;

  uint32_t cur_ptr=0x3E;
  for(int or=0;or<6;or++)
    for(int frm=0;frm<frm_num;frm++)
    {
      SpriteInfo* lpinf=new SpriteInfo;
      lpinf->offsetX=offs_x[or];
      lpinf->offsetY=offs_y[or];

      fm.SetCurrentPosition(cur_ptr);
      uint16_t w=fm.GetWord();
      uint16_t h=fm.GetWord();
      lpinf->width=w;
      lpinf->height=h;
      fm.GoForward(4);
      pframes->next_x[or*frm_num+frm]=fm.GetWord();
      pframes->next_y[or*frm_num+frm]=fm.GetWord();

      if(!lastSurface)
      {
        //спрайт будет помещен в новую поверхность
        lpinf->surface=CreateNewSurface(w,h);
        if(!lpinf->surface) return 0;
        lpinf->spriteRect(0,0,w,h);
        lastSurface=lpinf->surface;
      }
      else if( (last_w-free_x)>=w && (last_h-free_y)>=h )
      {
        //спрайт будет помещен в горизонтальный ряд
        lpinf->surface=lastSurface;
        lpinf->spriteRect(free_x,free_y,free_x+w,free_y+h);
      }
      else if( last_w>=w && (last_h-busy_h)>=h )
      {
        //начинаем новый горизонтальный ряд этим спрайтом
        free_x=0;
        free_y=busy_h;
        lpinf->surface=lastSurface;
        lpinf->spriteRect(free_x,free_y,free_x+w,free_y+h);
      }
      else
      {
        //спрайт вообще не поместился на эту поверхность
        lpinf->surface=CreateNewSurface(w,h);
        if(!lpinf->surface) return 0;
        lpinf->spriteRect(0,0,w,h);
        lastSurface=lpinf->surface;
      }

      int aligned_width = (4 - w%4)%4;

      // создаем файл формата bmp в памяти
      uint32_t wpos=sizeof(bmpheader);
      uint8_t* res=new uint8_t[wpos+h*(w+aligned_width)];
      memcpy(res,bmpheader,wpos);


      uint32_t ptr = cur_ptr+12+w*(h-1);
      cur_ptr+=w*h+12;
      for(int i=0;i < h; i++)
      {
        fm.SetCurrentPosition(ptr);
        fm.Read(res+wpos,w);
        wpos+=w;
        memset(res+wpos,0,aligned_width);
        wpos+=aligned_width;
        ptr -= w;
      }

      uint32_t* spos=(uint32_t*)(res+18);
      spos[0]=w;
      spos[1]=h;


      //а теперь перенесем его на временную поверхность
      LPDIRECT3DSURFACE8 lpsurf;
      LPDIRECT3DSURFACE8 lptexsurf;
//      HRESULT hr=lpDevice->CreateImageSurface(w,h,D3DFMT_A8R8G8B8,&lpsurf);
      HRESULT hr=lpDevice->CreateImageSurface(w,h,TEX_FRMT,&lpsurf);
      if(hr!=D3D_OK){
        ReportErrorMessage("CSpriteManager LoadSprite","Anim Не могу создать Surface для файла %s",fname);
        return 0;
      }

      lastSurface->GetSurfaceLevel(0,&lptexsurf);

      hr=D3DXLoadSurfaceFromFileInMemory(lpsurf,NULL,NULL,res,wpos,NULL,D3DX_FILTER_NONE,D3DCOLOR_ARGB(255,0,0,0),NULL);
      if(hr!=D3D_OK){
        ReportErrorMessage("CSpriteManager LoadSprite","Не могу загрузить Surface из памяти для файла %s",fname);
        return 0;
      }

      POINT p={free_x,free_y};
      RECT r={0,0,w,h};

      //с которой уже скопируем спрайт в нужное место
      lpDevice->CopyRects(lpsurf,&r,1,lptexsurf,&p);
      if(hr!=D3D_OK){
        ReportErrorMessage("CSpriteManager LoadSprite","Ошибка при копировании поверхностей для файла %s",fname);
        return 0;
      }

      lpsurf->Release();
      lptexsurf->Release();

      delete[] res;

      free_x+=w;
      if((free_y+h)>busy_h) busy_h=free_y+h;
      lpinf->spriteRect.x1/=last_w;
      lpinf->spriteRect.x2/=last_w;
      lpinf->spriteRect.y1/=last_h;
      lpinf->spriteRect.y2/=last_h;

      spriteData[next_id++]=lpinf;

      pframes->ind[or*frm_num+frm]=next_id-1;
    }

    fm.UnloadFile();
//    FONLINE_LOG("%d frames loaded\n",frm_num*6);
    return 1;
}

int CSpriteManager::LoadRix(char *fname, int PathType)
{
  assert(initialized);
  assert(fname != NULL);
  assert(fname[0] != '\0');

  if(!initialized) return 0;
  if(!fname[0]) return 0;
  if(!fm.LoadFile(fname,PathType))
    return 0;

  SpriteInfo* lpinf=new SpriteInfo;
  fm.SetCurrentPosition(0x4);
  uint16_t w;fm.Read(&w,2);
  uint16_t h;fm.Read(&h,2);
  if(w!=640 || h!=480) return 0;
  lpinf->width=MODE_WIDTH;
  lpinf->height=MODE_HEIGHT;

  if(!lastSurface)
  {
    lpinf->surface=CreateNewSurface(MODE_WIDTH,MODE_HEIGHT);
    if(!lpinf->surface) return 0;
    lpinf->spriteRect(0,0,MODE_WIDTH,MODE_HEIGHT);
    lastSurface=lpinf->surface;
  }
  else if( (last_w-free_x)>=MODE_WIDTH && (last_h-free_y)>=MODE_HEIGHT )
  {
    lpinf->surface=lastSurface;
    lpinf->spriteRect(free_x,free_y,free_x+MODE_WIDTH,free_y+MODE_HEIGHT);
  }
  else if( last_w>=MODE_WIDTH && (last_h-busy_h)>=MODE_HEIGHT )
  {
    free_x=0;
    free_y=busy_h;
    lpinf->surface=lastSurface;
    lpinf->spriteRect(free_x,free_y,free_x+MODE_WIDTH,free_y+MODE_HEIGHT);
  }
  else
  {
    lpinf->surface=CreateNewSurface(MODE_WIDTH,MODE_HEIGHT);
    if(!lpinf->surface) return 0;
    lpinf->spriteRect(0,0,MODE_WIDTH,MODE_HEIGHT);
    lastSurface=lpinf->surface;
  }

  int aligned_width = (4 - w%4)%4;

  // let's write the bmp
  uint32_t wpos=sizeof(bmpheader);
  uint8_t* res=new uint8_t[wpos+h*(w+aligned_width)];
  memcpy(res,bmpheader,wpos);

  int i;

  //заменим палитру
  fm.SetCurrentPosition(0xA);
  uint8_t* ppos=res+sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);
  for(i=0;i<256;i++)
  {
    fm.Read(ppos,3);
    for(int j=0;j<3;j++)
      ppos[j]*=4;
    uint8_t t=ppos[2];
    ppos[2]=ppos[0];
    ppos[0]=t;
    ppos+=4;
  }


  uint32_t ptr = 0xA+256*3+w*(h-1);
  for(i=0;i < h; i++)
  {
    fm.SetCurrentPosition(ptr);
    fm.Read(res+wpos,w);
    wpos+=w;
    memset(res+wpos,0,aligned_width);
    wpos+=aligned_width;
    ptr -= w;
  }

  uint32_t* spos=(uint32_t*)(res+18);
  spos[0]=w;
  spos[1]=h;

  LPDIRECT3DSURFACE8 lpsurf;
  LPDIRECT3DSURFACE8 lptexsurf;
  //HRESULT hr=lpDevice->CreateImageSurface(w,h,D3DFMT_A8R8G8B8,&lpsurf);
  HRESULT hr=lpDevice->CreateImageSurface(MODE_WIDTH,MODE_HEIGHT,TEX_FRMT,&lpsurf);
  if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteManager LoadRix","Не могу создать Surface для файла %s",fname);
    return 0;
  }

  lastSurface->GetSurfaceLevel(0,&lptexsurf);

  hr=D3DXLoadSurfaceFromFileInMemory(lpsurf,NULL,NULL,res,wpos,NULL,D3DX_FILTER_LINEAR,D3DCOLOR_ARGB(255,0,0,0),NULL);
  if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteManager LoadRix","Не могу загрузить Surface из памяти для файла %s",fname);
    return 0;
  }

  POINT p={free_x,free_y};
  RECT r={0,0,MODE_WIDTH,MODE_HEIGHT};

  lpDevice->CopyRects(lpsurf,&r,1,lptexsurf,&p);
  if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteManager LoadRix","Ошибка при копировании поверхностей для файла %s",fname);
    return 0;
  }

  lpsurf->Release();
  lptexsurf->Release();

  delete[] res;

  free_x+=MODE_WIDTH;
  if((free_y+MODE_HEIGHT)>busy_h) busy_h=free_y+MODE_HEIGHT;
  lpinf->spriteRect.x1/=last_w;
  lpinf->spriteRect.x2/=last_w;
  lpinf->spriteRect.y1/=last_h;
  lpinf->spriteRect.y2/=last_h;

  spriteData[next_id++]=lpinf;

  fm.UnloadFile();
  return next_id-1;
}

// Для 1-но направленных анимаций
int CSpriteManager::LoadAnimationD(char *fname,int PathType,CritFrames* pframes)
{
  assert(initialized);
  assert(fname != NULL);
  assert(fname[0] != '\0');
  assert(pframes != NULL);

  if(!initialized) return 0;
  if(!fname[0]) return 0;

  char path[1024];
  uint16_t frm_num;
  short offs_x[6], offs_y[6];

  for(int or=0;or<6;or++)
  {
    strcpy(path,fname);
    if(or==0) strcat(path,"0");
    if(or==1) strcat(path,"1");
    if(or==2) strcat(path,"2");
    if(or==3) strcat(path,"3");
    if(or==4) strcat(path,"4");
    if(or==5) strcat(path,"5");

  //  FONLINE_LOG("Loading animation %s\n",path);
    if(!fm.LoadFile(path,PathType)) return 0;
  //  FONLINE_LOG("Loading offsets animation %s\n",path);

    fm.SetCurrentPosition(0x4); //!Cvet
    uint16_t frm_fps=fm.GetWord(); //!Cvet
    if(!frm_fps) frm_fps=10; //!Cvet

    fm.SetCurrentPosition(0x8);
    frm_num=fm.GetWord();

    //Получаем общие смещения по всем направлениям
    fm.SetCurrentPosition(0xA);
      offs_x[or]=fm.GetWord();
    fm.SetCurrentPosition(0x16);
      offs_y[or]=fm.GetWord();

    // Здеся вручную пробуем в 6 направлениях
    if(or==0)
    {
      pframes->ticks=1000/frm_fps*frm_num; //!Cvet
      pframes->cnt_frames=frm_num;
      pframes->ind=new uint16_t[frm_num*6];
      pframes->next_x=new short[frm_num*6];
      pframes->next_y=new short[frm_num*6];
    }

    pframes->dir_offs[or]=or*frm_num;

    uint32_t cur_ptr=0x3E;
    for(int frm=0;frm<frm_num;frm++)
    {
      SpriteInfo* lpinf=new SpriteInfo;
      lpinf->offsetX=offs_x[or];
      lpinf->offsetY=offs_y[or];

      fm.SetCurrentPosition(cur_ptr);
      uint16_t w=fm.GetWord();
      uint16_t h=fm.GetWord();
      lpinf->width=w;
      lpinf->height=h;
      fm.GoForward(4);
      pframes->next_x[or*frm_num+frm]=fm.GetWord();
      pframes->next_y[or*frm_num+frm]=fm.GetWord();

      if(!lastSurface)
      {
        //спрайт будет помещен в новую поверхность
        lpinf->surface=CreateNewSurface(w,h);
        if(!lpinf->surface) return 0;
        lpinf->spriteRect(0,0,w,h);
        lastSurface=lpinf->surface;
      }
      else if( (last_w-free_x)>=w && (last_h-free_y)>=h )
      {
        //спрайт будет помещен в горизонтальный ряд
        lpinf->surface=lastSurface;
        lpinf->spriteRect(free_x,free_y,free_x+w,free_y+h);
      }
      else if( last_w>=w && (last_h-busy_h)>=h )
      {
        //начинаем новый горизонтальный ряд этим спрайтом
        free_x=0;
        free_y=busy_h;
        lpinf->surface=lastSurface;
        lpinf->spriteRect(free_x,free_y,free_x+w,free_y+h);
      }
      else
      {
        //спрайт вообще не поместился на эту поверхность
        lpinf->surface=CreateNewSurface(w,h);
        if(!lpinf->surface) return 0;
        lpinf->spriteRect(0,0,w,h);
        lastSurface=lpinf->surface;
      }

      int aligned_width = (4 - w%4)%4;

      // создаем файл формата bmp в памяти
      uint32_t wpos=sizeof(bmpheader);
      uint8_t* res=new uint8_t[wpos+h*(w+aligned_width)];
      memcpy(res,bmpheader,wpos);


      uint32_t ptr = cur_ptr+12+w*(h-1);
      cur_ptr+=w*h+12;
      for(int i=0;i < h; i++)
      {
        fm.SetCurrentPosition(ptr);
        fm.Read(res+wpos,w);
        wpos+=w;
        memset(res+wpos,0,aligned_width);
        wpos+=aligned_width;
        ptr -= w;
      }

      uint32_t* spos=(uint32_t*)(res+18);
      spos[0]=w;
      spos[1]=h;


      //а теперь перенесем его на временную поверхность
      LPDIRECT3DSURFACE8 lpsurf;
      LPDIRECT3DSURFACE8 lptexsurf;
//      HRESULT hr=lpDevice->CreateImageSurface(w,h,D3DFMT_A8R8G8B8,&lpsurf);
      HRESULT hr=lpDevice->CreateImageSurface(w,h,TEX_FRMT,&lpsurf);
      if(hr!=D3D_OK){
        ReportErrorMessage("CSpriteManager LoadSprite","Anim Не могу создать Surface для файла %s",fname);
        return 0;
      }

      lastSurface->GetSurfaceLevel(0,&lptexsurf);

      hr=D3DXLoadSurfaceFromFileInMemory(lpsurf,NULL,NULL,res,wpos,NULL,D3DX_FILTER_NONE,D3DCOLOR_ARGB(255,0,0,0),NULL);
      if(hr!=D3D_OK){
        ReportErrorMessage("CSpriteManager LoadSprite","Не могу загрузить Surface из памяти для файла %s",fname);
        return 0;
      }

      POINT p={free_x,free_y};
      RECT r={0,0,w,h};

      //с которой уже скопируем спрайт в нужное место
      lpDevice->CopyRects(lpsurf,&r,1,lptexsurf,&p);
      if(hr!=D3D_OK){
        ReportErrorMessage("CSpriteManager LoadSprite","Ошибка при копировании поверхностей для файла %s",fname);
        return 0;
      }

      lpsurf->Release();
      lptexsurf->Release();

      delete[] res;

      free_x+=w;
      if((free_y+h)>busy_h) busy_h=free_y+h;
      lpinf->spriteRect.x1/=last_w;
      lpinf->spriteRect.x2/=last_w;
      lpinf->spriteRect.y1/=last_h;
      lpinf->spriteRect.y2/=last_h;

      spriteData[next_id++]=lpinf;

      pframes->ind[or*frm_num+frm]=next_id-1;
    }

    fm.UnloadFile();}
//    FONLINE_LOG("%d frames loaded\n",frm_num*6);
    return 1;
}

// Для любых спрайтовых анимаций
int CSpriteManager::LoadAnyAnimation(char *fname,int PathType, AnyFrames* aanim, SpriteInfo** ppInfo)
{
  assert(initialized);
  assert(fname != NULL);
  assert(fname[0] != '\0');
  assert(aanim != NULL);
  assert(ppInfo != NULL);

  if(!initialized) return 0;
  if(!fname[0]) return 0;

  char path[1024];
  uint16_t frm_num;
  short offs_x, offs_y;

  strcpy(path,fname);

//  FONLINE_LOG("Loading animation %s\n",path);
  if(!fm.LoadFile(path,PathType)) return 0;

//!Cvet +++++++++
  fm.SetCurrentPosition(0x4);
  uint16_t frm_fps=fm.GetWord();
  if(!frm_fps) frm_fps=10;

  fm.SetCurrentPosition(0x8);
  frm_num=fm.GetWord();

  fm.SetCurrentPosition(0xA);
  offs_x=fm.GetWord();
  aanim->offs_x=offs_x;
  fm.SetCurrentPosition(0x16);
  offs_y=fm.GetWord();
  aanim->offs_y=offs_y;

  aanim->cnt_frames=frm_num;
  aanim->ticks=1000/frm_fps*frm_num;

  aanim->ind=new uint16_t[frm_num];
  aanim->next_x=new short[frm_num];
  aanim->next_y=new short[frm_num];
//!Cvet ---------

  uint32_t cur_ptr=0x3E;
  // Здесь включить цикл на количество ФРМ-ок
  for(int frm=0;frm<frm_num;frm++)
  {
    SpriteInfo* lpinf=new SpriteInfo;
//!Cvet +++++++++ был сумбур какойто. по всей функции...
    fm.SetCurrentPosition(cur_ptr);

    uint16_t w=fm.GetWord();
    uint16_t h=fm.GetWord();
    lpinf->width=w;
    lpinf->height=h;

    fm.GoForward(4);

    lpinf->offsetX=offs_x;
    lpinf->offsetY=offs_y;

    aanim->next_x[frm]=fm.GetWord();
    aanim->next_y[frm]=fm.GetWord();
//!Cvet ---------

    if(!lastSurface)
    {
      lpinf->surface=CreateNewSurface(w,h);
      if(!lpinf->surface) return 0;
      lpinf->spriteRect(0,0,w,h);
      lastSurface=lpinf->surface;
    }
    else if( (last_w-free_x)>=w && (last_h-free_y)>=h )
    {
      lpinf->surface=lastSurface;
      lpinf->spriteRect(free_x,free_y,free_x+w,free_y+h);
    }
    else if( last_w>=w && (last_h-busy_h)>=h )
    {
      free_x=0;
      free_y=busy_h;
      lpinf->surface=lastSurface;
      lpinf->spriteRect(free_x,free_y,free_x+w,free_y+h);
    }
    else
    {
      lpinf->surface=CreateNewSurface(w,h);
      if(!lpinf->surface) return 0;
      lpinf->spriteRect(0,0,w,h);
      lastSurface=lpinf->surface;
    }

    int aligned_width = (4 - w%4)%4;

    // let's write the bmp
    uint32_t wpos=sizeof(bmpheader);
    uint8_t* res=new uint8_t[wpos+h*(w+aligned_width)];
    memcpy(res,bmpheader,wpos);
    // Сдвиг по картинке в нужных координатах
    uint32_t ptr = cur_ptr+12+w*(h-1);
    cur_ptr+=w*h+12;
    for(int i=0;i < h; i++)
    {
      fm.SetCurrentPosition(ptr);
      fm.Read(res+wpos,w);
      wpos+=w;
      memset(res+wpos,0,aligned_width);
      wpos+=aligned_width;
      ptr -= w;
    }
/*
  uint32_t ptr = 0x4A+w*(h-1);
  for(int i=0;i < h; i++)
  {
    fm.SetCurrentPosition(ptr);
    fm.Read(res+wpos,w);
    wpos+=w;
    memset(res+wpos,0,aligned_width);
    wpos+=aligned_width;
    ptr -= w;
  }*/

    uint32_t* spos=(uint32_t*)(res+18);
    spos[0]=w;
    spos[1]=h;


    LPDIRECT3DSURFACE8 lpsurf;
    LPDIRECT3DSURFACE8 lptexsurf;
    //HRESULT hr=lpDevice->CreateImageSurface(w,h,D3DFMT_A8R8G8B8,&lpsurf);
    HRESULT hr=lpDevice->CreateImageSurface(w,h,TEX_FRMT,&lpsurf);
    if(hr!=D3D_OK)
    {
      ReportErrorMessage("CSpriteManager LoadSprite","Не могу создать Surface для файла %s",fname);
      return 0;
    }

    lastSurface->GetSurfaceLevel(0,&lptexsurf);

    hr=D3DXLoadSurfaceFromFileInMemory(lpsurf,NULL,NULL,res,wpos,NULL,D3DX_FILTER_NONE,D3DCOLOR_ARGB(255,0,0,0),NULL);
    if(hr!=D3D_OK)
    {
      ReportErrorMessage("CSpriteManager LoadSprite","Не могу загрузить Surface из памяти для файла %s",fname);
      return 0;
    }

    POINT p={free_x,free_y};
    RECT r={0,0,w,h};

    lpDevice->CopyRects(lpsurf,&r,1,lptexsurf,&p);
    if(hr!=D3D_OK)
    {
      ReportErrorMessage("CSpriteManager LoadSprite","Ошибка при копировании поверхностей для файла %s",fname);
      return 0;
    }

    lpsurf->Release();
    lptexsurf->Release();

    delete[] res;

    free_x+=w;
    if((free_y+h)>busy_h) busy_h=free_y+h;
    lpinf->spriteRect.x1/=last_w;
    lpinf->spriteRect.x2/=last_w;
    lpinf->spriteRect.y1/=last_h;
    lpinf->spriteRect.y2/=last_h;

    spriteData[next_id++]=lpinf;
    aanim->ind[frm]=next_id-1;
//    FONLINE_LOG("Loading animation!%d\n",frm);
  }
  // конец загрузки всех фрм на подготовленные поверхности
  fm.UnloadFile();

//  FONLINE_LOG("%d frames loaded\n",frm_num);
  return 1;
}

//-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-//

LPDIRECT3DTEXTURE8 CSpriteManager::CreateNewSurface(uint16_t w, uint16_t h)
{
  assert(initialized);

  if (w > opt_basetex || h > opt_basetex) {
    for (last_w = opt_basetex; last_w < w; last_w *= 2);
    for (last_h = opt_basetex; last_h < h; last_h *= 2);
  } else {
    last_w=opt_basetex;
    last_h=opt_basetex;
  }

  LPDIRECT3DTEXTURE8 surface = NULL;

  HRESULT hr = lpDevice->CreateTexture(last_w, last_h, 1, 0, TEX_FRMT, D3DPOOL_MANAGED, &surface);
  if (hr != D3D_OK) {
    ReportErrorMessage("CSpriteManager CreateNewSurface", "Could not create a new surface");
    return NULL;
  }

  surfaceList.push_back(surface);
  busy_w = busy_h = free_x = free_y = 0;

  return surface;
}

void CSpriteManager::NextSurface() {
  assert(initialized);
  lastSurface=NULL;
}

//-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-//

int CSpriteManager::Flush() {
  assert(initialized);

  void* buffer = NULL;
  int quadCount = 4 * currentPosition;

  lpVB->Lock(0, sizeof(Vertex) * quadCount, (BYTE**) &buffer, D3DLOCK_DISCARD);
  memcpy(buffer, lpWaitBuf, sizeof(Vertex) * quadCount);
  lpVB->Unlock();

  //рисуем спрайты
  if (!call_vec.empty()) {
    uint16_t rpos = 0;

    for (onesurf_vec::iterator iv = call_vec.begin(); iv != call_vec.end(); ++iv) {
      lpDevice->SetTexture(0,(*iv)->lpSurf);
      lpDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, quadCount, rpos, 2 * (*iv)->cnt);
      rpos += 6 * (*iv)->cnt;
      delete (*iv);
    }

    call_vec.clear();
    last_call = NULL;
    currentSurface = NULL;
  } else {
    lpDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, quadCount, 0, 2 * currentPosition);
  }

  currentPosition = 0;
  return 1;
}

int CSpriteManager::DrawSprite(uint16_t id, int x, int y, uint32_t color, uint32_t alpha) {
  assert(initialized);

  SpriteInfo* info = spriteData[id];
  if (!info) {
    return 0;
  }

  if (currentSurface != info->surface) {
    last_call = new OneSurface(info->surface);
    call_vec.push_back(last_call);
    currentSurface = info->surface;
  } else if(last_call) {
    last_call->cnt++;
  }

  int mulpos=currentPosition*4;

  if (!color) color = col; //!Cvet
  if (alpha) color += alpha << 24;

  lpWaitBuf[mulpos].x=x-0.5f;
  lpWaitBuf[mulpos].y=y+info->height-0.5f;
  lpWaitBuf[mulpos].tu=info->spriteRect.x1;
  lpWaitBuf[mulpos].tv=info->spriteRect.y2;
  lpWaitBuf[mulpos++].Diffuse=color;

  lpWaitBuf[mulpos].x=x-0.5f;
  lpWaitBuf[mulpos].y=y-0.5f;
  lpWaitBuf[mulpos].tu=info->spriteRect.x1;
  lpWaitBuf[mulpos].tv=info->spriteRect.y1;
  lpWaitBuf[mulpos++].Diffuse=color;

  lpWaitBuf[mulpos].x=x+info->width-0.5f;
  lpWaitBuf[mulpos].y=y-0.5f;
  lpWaitBuf[mulpos].tu=info->spriteRect.x2;
  lpWaitBuf[mulpos].tv=info->spriteRect.y1;
  lpWaitBuf[mulpos++].Diffuse=color;

  lpWaitBuf[mulpos].x=x+info->width-0.5f;
  lpWaitBuf[mulpos].y=y+info->height-0.5f;
  lpWaitBuf[mulpos].tu=info->spriteRect.x2;
  lpWaitBuf[mulpos].tv=info->spriteRect.y2;
  lpWaitBuf[mulpos++].Diffuse=color;

  currentPosition++;

  if(currentPosition==maxSpriteCount) Flush();

  return 1;
}
/*
void CSpriteManager::DrawPrepPix(Pix_vec* prep_pix)
{
  if(prep_pix->empty()) return;

  Flush();

  int cnt_pix=prep_pix->size();

  LPDIRECT3DVERTEXBUFFER8 p_VB;

  lpDevice->CreateVertexBuffer(cnt_pix*sizeof(CUSTOMVERTEX),
    D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, //D3DUSAGE_POINTS
    D3DFVF_CUSTOMVERTEX, D3DPOOL_DEFAULT,
    &p_VB);

  CUSTOMVERTEX* g_Vertices=new CUSTOMVERTEX[cnt_pix];

  int cur_pix=0;
  for(Pix_vec::iterator it_p=prep_pix->begin();it_p!=prep_pix->end();it_p++)
  {
    g_Vertices[cur_pix].x=(*it_p)->x;
    g_Vertices[cur_pix].y=(*it_p)->y;
    if((*it_p)->lp_ox) g_Vertices[cur_pix].x+=*(*it_p)->lp_ox;
    if((*it_p)->lp_oy) g_Vertices[cur_pix].y+=*(*it_p)->lp_oy;
    g_Vertices[cur_pix].Diffuse=(*it_p)->color;
    g_Vertices[cur_pix].z=0.0f;
    g_Vertices[cur_pix].rhw=1.0f;

    cur_pix++;
  }

  VOID* pVertices;
  p_VB->Lock (0, cnt_pix*sizeof(CUSTOMVERTEX), (uint8_t**)&pVertices, D3DLOCK_DISCARD);
  memcpy (pVertices, g_Vertices, cnt_pix*sizeof(CUSTOMVERTEX));
  p_VB->Unlock();

  SAFEDELA(g_Vertices);

  lpDevice->SetStreamSource(0,p_VB,sizeof(CUSTOMVERTEX));

  lpDevice->SetTextureStageState(0,D3DTSS_MAGFILTER,D3DTEXF_POINT);
//  lpDevice->SetTextureStageState(0,D3DTSS_MINFILTER,D3DTEXF_POINT);
  lpDevice->SetVertexShader(D3DFVF_CUSTOMVERTEX);

  lpDevice->DrawPrimitive(D3DPT_POINTLIST, 0, cnt_pix);

//  lpDevice->SetTextureStageState(0,D3DTSS_MINFILTER,D3DTEXF_LINEAR);
  lpDevice->SetTextureStageState(0,D3DTSS_MAGFILTER,D3DTEXF_LINEAR);

  SAFEREL(p_VB);

  lpDevice->SetStreamSource(0,lpVB,sizeof(Vertex));
  lpDevice->SetVertexShader(D3DFVF_VERTEX_FORMAT);
//  lpDevice->SetTexture(0,last_call->surface);
//  lpDevice->SetIndices(lpIB,0);
//  lpDevice->SetVertexShader(D3DFVF_VERTEX_FORMAT);

//  FONLINE_LOG("p:%d\n",prep_pix->size());
}
*/

int CSpriteManager::DrawSpriteSize(uint16_t id, int x, int y,double size, uint32_t color) //!Cvet uint32_t color
{
  assert(initialized);

  SpriteInfo* lpinf=spriteData[id];
  if(!lpinf) return 0;
  //lpinf->spriteRect.x1/=size;
  //lpinf->spriteRect.x1/=size;

  if(currentSurface!=lpinf->surface)
  {
    last_call=new OneSurface(lpinf->surface);
    call_vec.push_back(last_call);
    currentSurface=lpinf->surface;
  }
  else if(last_call) last_call->cnt++;

  int mulpos=currentPosition*4;

  if(!color) color=col; //!Cvet

  lpWaitBuf[mulpos].x=x-0.5f;
  lpWaitBuf[mulpos].y=y+lpinf->height-0.5f;
  lpWaitBuf[mulpos].tu=lpinf->spriteRect.x1;
  lpWaitBuf[mulpos].tv=lpinf->spriteRect.y2;
  lpWaitBuf[mulpos++].Diffuse=color;

  lpWaitBuf[mulpos].x=x-0.5f;
  lpWaitBuf[mulpos].y=y-0.5f;
  lpWaitBuf[mulpos].tu=lpinf->spriteRect.x1;
  lpWaitBuf[mulpos].tv=lpinf->spriteRect.y1;
  lpWaitBuf[mulpos++].Diffuse=color;

  lpWaitBuf[mulpos].x=x+lpinf->width-0.5f;
  lpWaitBuf[mulpos].y=y-0.5f;
  lpWaitBuf[mulpos].tu=lpinf->spriteRect.x2;
  lpWaitBuf[mulpos].tv=lpinf->spriteRect.y1;
  lpWaitBuf[mulpos++].Diffuse=color;

  lpWaitBuf[mulpos].x=x+lpinf->width-0.5f;
  lpWaitBuf[mulpos].y=y+lpinf->height-0.5f;
  lpWaitBuf[mulpos].tu=lpinf->spriteRect.x2;
  lpWaitBuf[mulpos].tv=lpinf->spriteRect.y2;
  lpWaitBuf[mulpos++].Diffuse=color;

  currentPosition++;

  if(currentPosition==maxSpriteCount) Flush();

  return 1;
}

int CSpriteManager::PrepareBuffer(dtree_map* lpdtree,LPDIRECT3DVERTEXBUFFER8* lplpBuf,onesurf_vec* lpsvec, uint32_t color, uint8_t alpha)
{
  assert(initialized);
  assert(lpdtree != NULL);
  assert(lpsvec != NULL);

  if ((*lplpBuf) != NULL) {
    (*lplpBuf)->Release();
    *lplpBuf = NULL;
  }
  for(onesurf_vec::iterator iv=lpsvec->begin();iv!=lpsvec->end();iv++)
    delete (*iv);
  lpsvec->clear();

  uint16_t cnt=lpdtree->size();

  if(!cnt) return 1;

  //Создаем буфер вершин
  HRESULT hr=lpDevice->CreateVertexBuffer(cnt*4*sizeof(Vertex),D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC,
    D3DFVF_VERTEX_FORMAT,D3DPOOL_DEFAULT,lplpBuf);
  if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteMngr PrepareBuffer","Ошибка при создании буфера: %s",DXGetErrorString8(hr));
    return 0;
  }

  uint32_t need_size=cnt*6*sizeof(uint16_t);
  D3DINDEXBUFFER_DESC ibdesc;

  lpIB->GetDesc(&ibdesc);

  if(ibdesc.Size<need_size)
  {
    if (lpIB != NULL) {
      lpIB->Release();
      lpIB = NULL;
    }
    //и индексов
    hr=lpDevice->CreateIndexBuffer(need_size,D3DUSAGE_WRITEONLY,
      D3DFMT_INDEX16,D3DPOOL_DEFAULT,&lpIB);
    if(hr!=D3D_OK){
    ReportErrorMessage("CSpriteMngr PrepareBuffer","Ошибка при создании буфера: %s",DXGetErrorString8(hr));
      return 0;
    }

    uint16_t* IndexList=new uint16_t[6*cnt];
    for(int i=0;i<cnt;i++)
    {
      IndexList[6*i+0]=4*i+0;
      IndexList[6*i+1]=4*i+1;
      IndexList[6*i+2]=4*i+3;
      IndexList[6*i+3]=4*i+1;
      IndexList[6*i+4]=4*i+2;
      IndexList[6*i+5]=4*i+3;
    }

    void* pBuffer;
    lpIB->Lock(0,0,(uint8_t**)&pBuffer,0);
      memcpy(pBuffer,IndexList,need_size);
    lpIB->Unlock();

    delete[] IndexList;

    lpDevice->SetIndices(lpIB,0);
  }

  uint16_t mulpos=0;
  OneSurface* lc=NULL;
  Vertex* localBuf=new Vertex[cnt*4];

  uint32_t new_color=col; //!Cvet
  if(color) new_color=(new_color & 0xFF000000) + (color & 0xFFFFFF); //!Cvet
  if(alpha) new_color=(new_color & 0xFFFFFF) + ((alpha << 24) & 0xFF000000); //!Cvet

//!Cvet оптимизировал. спрайты сортируються по поверхности. производительность выросла +++++++++++++++++++++
  spr_vec_vec spr_vectors;
  spr_vec_vec::iterator it_vec;
  for(dtree_map::iterator jt=lpdtree->begin();jt!=lpdtree->end();jt++)
  {
    SpriteInfo* sinf=spriteData[(*jt).second->spr_id];
    if(!sinf) return 0;

    for(it_vec=spr_vectors.begin();it_vec!=spr_vectors.end();it_vec++)
    {
      spr_vec* cur_spr_vec=&(*it_vec);
      PrepSprite* cur_ps=*cur_spr_vec->begin();
      SpriteInfo* cur_sinf=spriteData[cur_ps->spr_id];
      if(cur_sinf->surface==sinf->surface)
      {
        cur_spr_vec->push_back((*jt).second);
        break;
      }
    }

    if(it_vec==spr_vectors.end())
    {
      spr_vec n_vec;
      n_vec.push_back((*jt).second);
      spr_vectors.push_back(n_vec);
    }
  }

  for(it_vec=spr_vectors.begin();it_vec!=spr_vectors.end();it_vec++)
    for(spr_vec::iterator it_spr=(*it_vec).begin();it_spr!=(*it_vec).end();it_spr++) //Cvet -------------------------
    {
      SpriteInfo* lpinf=spriteData[(*it_spr)->spr_id];
      int x=(*it_spr)->scr_x+cmn_scr_ox;
      int y=(*it_spr)->scr_y+cmn_scr_oy;

      if((*it_spr)->lp_ox) x+=*(*it_spr)->lp_ox;
      if((*it_spr)->lp_oy) y+=*(*it_spr)->lp_oy;

      if(!lc || lc->lpSurf!=lpinf->surface)
      {
        lc=new OneSurface(lpinf->surface);
        lpsvec->push_back(lc);
      }
      else lc->cnt++;

      localBuf[mulpos].x=x-0.5f;
      localBuf[mulpos].y=y+lpinf->height-0.5f;
      localBuf[mulpos].tu=lpinf->spriteRect.x1;
      localBuf[mulpos].tv=lpinf->spriteRect.y2;
      localBuf[mulpos++].Diffuse=new_color;

      localBuf[mulpos].x=x-0.5f;
      localBuf[mulpos].y=y-0.5f;
      localBuf[mulpos].tu=lpinf->spriteRect.x1;
      localBuf[mulpos].tv=lpinf->spriteRect.y1;
      localBuf[mulpos++].Diffuse=new_color;

      localBuf[mulpos].x=x+lpinf->width-0.5f;
      localBuf[mulpos].y=y-0.5f;
      localBuf[mulpos].tu=lpinf->spriteRect.x2;
      localBuf[mulpos].tv=lpinf->spriteRect.y1;
      localBuf[mulpos++].Diffuse=new_color;

      localBuf[mulpos].x=x+lpinf->width-0.5f;
      localBuf[mulpos].y=y+lpinf->height-0.5f;
      localBuf[mulpos].tu=lpinf->spriteRect.x2;
      localBuf[mulpos].tv=lpinf->spriteRect.y2;
      localBuf[mulpos++].Diffuse=new_color;
    }

  void* pBuffer;
  (*lplpBuf)->Lock(0,sizeof(Vertex)*mulpos,(uint8_t**)&pBuffer,D3DLOCK_DISCARD);
    memcpy(pBuffer,localBuf,sizeof(Vertex)*mulpos);
  (*lplpBuf)->Unlock();

  if (localBuf != NULL) {
  delete [] localBuf;
  localBuf = NULL;
  }

  return 1;
}

void CSpriteManager::DrawPrepared(LPDIRECT3DVERTEXBUFFER8 lpBuf,onesurf_vec* lpsvec, uint16_t cnt)
{
  assert(initialized);
  assert(lpBuf != NULL);
  assert(lpsvec != NULL);

  if(!cnt) return;
  Flush();

  lpDevice->SetStreamSource(0,lpBuf,sizeof(Vertex));

  uint16_t rpos=0;
  for (onesurf_vec::iterator iv = lpsvec->begin(); iv != lpsvec->end(); ++iv) {
    lpDevice->SetTexture(0,(*iv)->lpSurf);
    lpDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,cnt*4,rpos,2*(*iv)->cnt);
    rpos+=6*(*iv)->cnt;
  }

  lpDevice->SetStreamSource(0,lpVB,sizeof(Vertex));
}

void CSpriteManager::GetDrawCntrRect(PrepSprite* prep, fonline::math::Rect<int>* prect)
{
  assert(initialized);
  assert(prep != NULL);
  assert(prect != NULL);

  uint16_t id;
  if(prep->lp_sprid) id=*prep->lp_sprid;
  else id=prep->spr_id;
  SpriteInfo* lpinf=spriteData[id];
  if(!lpinf) return;
  int x=prep->scr_x-(lpinf->width >> 1)+lpinf->offsetX;
  int y=prep->scr_y-lpinf->height+lpinf->offsetY;

  if(prep->lp_ox) x+=*prep->lp_ox;
  if(prep->lp_oy) y+=*prep->lp_oy;

  prect->left = x;
  prect->top = y;
  prect->right = x+lpinf->width;
  prect->bottom = y+lpinf->height;
}

void CSpriteManager::DrawTreeCntr(dtree_map* lpdtree)
{
  assert(initialized);
  assert(lpdtree != NULL);

  for(dtree_map::iterator jt=lpdtree->begin();jt!=lpdtree->end();jt++)
  {
    uint16_t id;
    if((*jt).second->lp_sprid) id=*(*jt).second->lp_sprid;
    else id=(*jt).second->spr_id;
    SpriteInfo* lpinf=spriteData[id];
    if(!lpinf) continue;
    int x=(*jt).second->scr_x-(lpinf->width >> 1)+lpinf->offsetX+cmn_scr_ox;
    int y=(*jt).second->scr_y-lpinf->height+lpinf->offsetY+cmn_scr_oy;

    if((*jt).second->lp_ox) x+=*(*jt).second->lp_ox;
    if((*jt).second->lp_oy) y+=*(*jt).second->lp_oy;

    if(currentSurface!=lpinf->surface)
    {
      last_call=new OneSurface(lpinf->surface);
      call_vec.push_back(last_call);
      currentSurface=lpinf->surface;
    }
    else if(last_call) last_call->cnt++;

    int mulpos=currentPosition*4;

    uint32_t cur_color=col;
    if((*jt).second->alpha) cur_color+=((uint32_t)(*(*jt).second->alpha)<<24) & 0xFF000000;

    lpWaitBuf[mulpos].x=x-0.5f;
    lpWaitBuf[mulpos].y=y+lpinf->height-0.5f;
    lpWaitBuf[mulpos].tu=lpinf->spriteRect.x1;
    lpWaitBuf[mulpos].tv=lpinf->spriteRect.y2;
    lpWaitBuf[mulpos++].Diffuse=cur_color;

    lpWaitBuf[mulpos].x=x-0.5f;
    lpWaitBuf[mulpos].y=y-0.5f;
    lpWaitBuf[mulpos].tu=lpinf->spriteRect.x1;
    lpWaitBuf[mulpos].tv=lpinf->spriteRect.y1;
    lpWaitBuf[mulpos++].Diffuse=cur_color;

    lpWaitBuf[mulpos].x=x+lpinf->width-0.5f;
    lpWaitBuf[mulpos].y=y-0.5f;
    lpWaitBuf[mulpos].tu=lpinf->spriteRect.x2;
    lpWaitBuf[mulpos].tv=lpinf->spriteRect.y1;
    lpWaitBuf[mulpos++].Diffuse=cur_color;

    lpWaitBuf[mulpos].x=x+lpinf->width-0.5f;
    lpWaitBuf[mulpos].y=y+lpinf->height-0.5f;
    lpWaitBuf[mulpos].tu=lpinf->spriteRect.x2;
    lpWaitBuf[mulpos].tv=lpinf->spriteRect.y2;
    lpWaitBuf[mulpos++].Diffuse=cur_color;

    currentPosition++;

    if(currentPosition==maxSpriteCount) Flush();
  }
}

void CSpriteManager::PreRestore()
{
  assert(initialized);

  if (lpVB != NULL) {
    lpVB->Release();
    lpVB = NULL;
  }
  if (lpIB != NULL) {
    lpIB->Release();
    lpIB = NULL;
  }
}

void CSpriteManager::PostRestore()
{
  assert(initialized);

  //Создаем буфер вершин
  FONLINE_LOG("Пересоздаю VB на %d спрайтов\n",maxSpriteCount);
  HRESULT hr=lpDevice->CreateVertexBuffer(maxSpriteCount*4*sizeof(Vertex),D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC,
    D3DFVF_VERTEX_FORMAT,D3DPOOL_DEFAULT,&lpVB);
  if(hr!=D3D_OK){
    ReportErrorMessage("SM::CreateVertexBuffer",(char*)DXGetErrorString8(hr));
    return;
  }

  //и индексов
  hr=lpDevice->CreateIndexBuffer(maxSpriteCount*6*sizeof(uint16_t),D3DUSAGE_WRITEONLY,
    D3DFMT_INDEX16,D3DPOOL_DEFAULT,&lpIB);
  if(hr!=D3D_OK){
    ReportErrorMessage("SM::CreateIndexBuffer",(char*)DXGetErrorString8(hr));
    return;
  }


  uint16_t* IndexList=new uint16_t[6*maxSpriteCount];
  for(int i=0;i<maxSpriteCount;i++)
  {
    IndexList[6*i+0]=4*i+0;
    IndexList[6*i+1]=4*i+1;
    IndexList[6*i+2]=4*i+3;
    IndexList[6*i+3]=4*i+1;
    IndexList[6*i+4]=4*i+2;
    IndexList[6*i+5]=4*i+3;
  }

  void* pBuffer;
  lpIB->Lock(0,0,(uint8_t**)&pBuffer,0);
    memcpy(pBuffer,IndexList,maxSpriteCount*6*sizeof(uint16_t));
  lpIB->Unlock();

  delete[] IndexList;

  lpDevice->SetIndices(lpIB,0);
  lpDevice->SetStreamSource(0,lpVB,sizeof(Vertex));
  lpDevice->SetVertexShader(D3DFVF_VERTEX_FORMAT);
}

//!Cvet +++++++++++++++++++++++++++++++++++++++++++
int CSpriteManager::LoadCritterTypes()
{
  //assert(initialized);

  //загружаем список криттеров

  IniFile::RecordMap ini;
  IniFile::LoadINI(opt_crfol, ini);

  //char str[1024];
  char key[64];
  CritterType cur = 0;
  CritterType cnt = IniFile::GetValue<int>(ini, "critters.id_cnt", 0);
  //GetPrivateProfileInt("critters","id_cnt",0,opt_crfol.c_str());
  if(!cnt) return 0;

  for(cur=0;cur<cnt;cur++)
  {
    wsprintf(key,"critters.%d",cur);
    std::string str = IniFile::GetValue<std::string>(ini, key, "");
    //GetPrivateProfileString("critters",key,"",str,1023,opt_crfol.c_str());
    if (str == "") continue;
    crit_types[cur] = str;
  }
  return 1;
}

int CSpriteManager::LoadCritterAnimation(CritterType anim_type, uint8_t anim_ind1, uint8_t anim_ind2)
{
  assert(initialized);

  if(CrAnim[anim_type][anim_ind1][anim_ind2]) return 1;

  FONLINE_LOG("Загрузка анимации type=%d,ind1=%d,ind2=%d...",anim_type,anim_ind1,anim_ind2);

  TICK loadA=GetTickCount();
  char frm_ind1[]="_ABCDEFGHIJKLMN___R";
  char frm_ind2[]="_ABCDEFGHIJKLMNOPQRST";
  char path[1024];//12345678901234567890

  sprintf(path,"%s%c%c.frm",crit_types[anim_type].c_str(),frm_ind1[anim_ind1],frm_ind2[anim_ind2]);
  FONLINE_LOG("1 попытка |%s|...",path);
  CrAnim[anim_type][anim_ind1][anim_ind2]=new CritFrames;
  if(!LoadAnimation(path,PT_ART_CRITTERS,CrAnim[anim_type][anim_ind1][anim_ind2]))
  {
    sprintf(path,"%s%c%c.fr",crit_types[anim_type].c_str(),frm_ind1[anim_ind1],frm_ind2[anim_ind2]);
    FONLINE_LOG("2 попытка |%s|...",path);
    if(!LoadAnimationD(path,PT_ART_CRITTERS,CrAnim[anim_type][anim_ind1][anim_ind2]))
    {
      CritFrames* frames = CrAnim[anim_type][anim_ind1][anim_ind2];

      if (frames != NULL) {
        delete frames;
        frames = NULL;
      }

      FONLINE_LOG("Анимация не найдена\n");
      return 0;
    }
  }
  FONLINE_LOG("OK - Время загрузки анимации %s =%d\n",path,GetTickCount()-loadA);
  return 1;
}

int CSpriteManager::EraseCritterAnimation(CritterType anim_type, uint8_t anim_ind1, uint8_t anim_ind2)
{
  assert(initialized);

  if(!CrAnim[anim_type][anim_ind1][anim_ind2]) return 1;
  TICK loadA=GetTickCount();

  sprinfo_map::iterator it;
  int num_frm=CrAnim[anim_type][anim_ind1][anim_ind2]->cnt_frames;
  for(int or=0;or<6;or++)
    for(int frm=0;frm<num_frm;frm++)
    {
      uint32_t num_sprite=or*num_frm+frm;
      it=spriteData.find(CrAnim[anim_type][anim_ind1][anim_ind2]->ind[num_sprite]);
      if(it==spriteData.end()) return 0;
      delete (*it).second;
      spriteData.erase(it);
    }

  CritFrames* frames = CrAnim[anim_type][anim_ind1][anim_ind2];

  if (frames != NULL) {
    delete frames;
    frames = NULL;
  }

  FONLINE_LOG("Время удаления анимации =%d\n",GetTickCount()-loadA);
  return 1;
}

int CSpriteManager::CheckPixTransp()
{
  assert(initialized);
  //lpDevice->GetPrivateData(NULL,NULL);

  return 0;
}

void CSpriteManager::DrawPrepPix(Pix_vec* prep_pix)
{
  assert(initialized);
  assert(prep_pix != NULL);

  if(prep_pix->empty()) return;

  Flush();

  int cnt_pix=prep_pix->size();

  LPDIRECT3DVERTEXBUFFER8 p_VB;

  lpDevice->CreateVertexBuffer(cnt_pix*sizeof(Vertex),
    D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, //D3DUSAGE_POINTS
    D3DFVF_VERTEX_FORMAT, D3DPOOL_DEFAULT,
    &p_VB);

  Vertex* g_Vertices=new Vertex[cnt_pix];

  int cur_pix=0;
  for(Pix_vec::iterator it_p=prep_pix->begin();it_p!=prep_pix->end();it_p++)
  {
    g_Vertices[cur_pix].x=(*it_p)->x;
    g_Vertices[cur_pix].y=(*it_p)->y;
    if((*it_p)->lp_ox) g_Vertices[cur_pix].x+=*(*it_p)->lp_ox;
    if((*it_p)->lp_oy) g_Vertices[cur_pix].y+=*(*it_p)->lp_oy;
    g_Vertices[cur_pix].Diffuse=(*it_p)->color;

    cur_pix++;
  }

  VOID* pVertices;
  p_VB->Lock (0, cnt_pix*sizeof(Vertex), (uint8_t**)&pVertices, D3DLOCK_DISCARD);
  memcpy (pVertices, g_Vertices, cnt_pix*sizeof(Vertex));
  p_VB->Unlock();

  if (g_Vertices != NULL) {
    delete [] g_Vertices;
    g_Vertices = NULL;
  }

  lpDevice->SetStreamSource(0,p_VB,sizeof(Vertex));

  lpDevice->SetTextureStageState(0,D3DTSS_COLOROP ,D3DTOP_DISABLE);
  lpDevice->SetTextureStageState(0,D3DTSS_MAGFILTER,D3DTEXF_POINT);
//  lpDevice->SetTextureStageState(0,D3DTSS_MINFILTER,D3DTEXF_POINT);

  lpDevice->DrawPrimitive(D3DPT_POINTLIST, 0, cnt_pix);

  lpDevice->SetTextureStageState(0,D3DTSS_COLOROP ,D3DTOP_MODULATE2X);
//  lpDevice->SetTextureStageState(0,D3DTSS_MINFILTER,D3DTEXF_LINEAR);
  lpDevice->SetTextureStageState(0,D3DTSS_MAGFILTER,D3DTEXF_LINEAR);

  if (p_VB != NULL) {
    p_VB->Release();
    p_VB = NULL;
  }

  lpDevice->SetStreamSource(0,lpVB,sizeof(Vertex));
}

void CSpriteManager::DrawPrepLines(Pix_vec* prep_pix)
{
  assert(initialized);
  assert(prep_pix != NULL);

  if(prep_pix->empty()) return;

  Flush();

  int cnt_pix=prep_pix->size();

  LPDIRECT3DVERTEXBUFFER8 p_VB;

  lpDevice->CreateVertexBuffer(cnt_pix*sizeof(Vertex),
    D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, //D3DUSAGE_POINTS
    D3DFVF_VERTEX_FORMAT, D3DPOOL_DEFAULT,
    &p_VB);

  Vertex* g_Vertices=new Vertex[cnt_pix];

  int cur_pix=0;
  for(Pix_vec::iterator it_p=prep_pix->begin();it_p!=prep_pix->end();it_p++)
  {
    g_Vertices[cur_pix].x=(*it_p)->x;
    g_Vertices[cur_pix].y=(*it_p)->y;
    if((*it_p)->lp_ox) g_Vertices[cur_pix].x+=*(*it_p)->lp_ox;
    if((*it_p)->lp_oy) g_Vertices[cur_pix].y+=*(*it_p)->lp_oy;
  //  if((*it_p)->lp_ox2) g_Vertices[cur_pix].x+=*(*it_p)->lp_ox2;
  //  if((*it_p)->lp_oy2) g_Vertices[cur_pix].y+=*(*it_p)->lp_oy2;
    g_Vertices[cur_pix].Diffuse=(*it_p)->color;

    cur_pix++;
  }

  VOID* pVertices;
  p_VB->Lock (0, cnt_pix*sizeof(Vertex), (uint8_t**)&pVertices, D3DLOCK_DISCARD);
  memcpy (pVertices, g_Vertices, cnt_pix*sizeof(Vertex));
  p_VB->Unlock();

  if (g_Vertices != NULL) {
    delete [] g_Vertices;
    g_Vertices = NULL;
  }

  lpDevice->SetStreamSource(0,p_VB,sizeof(Vertex));

  lpDevice->SetTextureStageState(0,D3DTSS_COLOROP ,D3DTOP_DISABLE);
  lpDevice->SetTextureStageState(0,D3DTSS_MAGFILTER,D3DTEXF_POINT);
//  lpDevice->SetTextureStageState(0,D3DTSS_MINFILTER,D3DTEXF_POINT);

  lpDevice->DrawPrimitive(D3DPT_LINELIST, 0, cnt_pix/2);

  lpDevice->SetTextureStageState(0,D3DTSS_COLOROP ,D3DTOP_MODULATE2X);
//  lpDevice->SetTextureStageState(0,D3DTSS_MINFILTER,D3DTEXF_LINEAR);
  lpDevice->SetTextureStageState(0,D3DTSS_MAGFILTER,D3DTEXF_LINEAR);

  if (p_VB != NULL) {
    p_VB->Release();
    p_VB = NULL;
  }

  lpDevice->SetStreamSource(0,lpVB,sizeof(Vertex));
}
//!Cvet -----------------------------------------
