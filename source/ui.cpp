#include "ui.hpp"
#include "ui_text.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace nmguardian {
namespace {
constexpr int W=1280, H=720;
using Color=uint32_t;
constexpr Color rgb(unsigned r,unsigned g,unsigned b) { return r|(g<<8)|(b<<16)|0xff000000u; }
constexpr Color ink=rgb(227,239,254), muted=rgb(143,164,192), dim=rgb(85,113,146);
constexpr Color cyan=rgb(62,183,255), violet=rgb(167,246,67), pink=rgb(255,198,67);
constexpr Color green=rgb(167,246,67), orange=rgb(255,187,58), magenta=rgb(167,246,67);
constexpr Color panel=rgb(12,24,44), raised=rgb(18,36,61), border=rgb(32,57,82);
struct Glyph {int w=0,h=0,advance=0;std::vector<uint8_t> alpha;};
struct Font {std::array<Glyph,224> glyphs;};
std::map<int,Font> fonts;
std::vector<uint8_t> rawFont, logo;
std::array<uint8_t,224> advances{};
std::vector<Color> background;
Color *fb=nullptr;
int pitch=W;

void pixel(int x,int y,Color c) {if(x>=0&&x<W&&y>=0&&y<H) fb[y*pitch+x]=c;}
void rect(int x,int y,int w,int h,Color c) {
    int x0=std::max(0,x),x1=std::min(W,x+w),y0=std::max(0,y),y1=std::min(H,y+h);
    if(x1<=x0||y1<=y0) return;
    for(int yy=y0;yy<y1;++yy) std::fill(fb+yy*pitch+x0,fb+yy*pitch+x1,c);
}
Color blend(Color dst,Color src,unsigned a) {
    unsigned r=(((dst&255)*(255-a)+(src&255)*a)+127)/255;
    unsigned g=((((dst>>8)&255)*(255-a)+((src>>8)&255)*a)+127)/255;
    unsigned b=((((dst>>16)&255)*(255-a)+((src>>16)&255)*a)+127)/255;
    return rgb(r,g,b);
}
void alphaPixel(int x,int y,Color c,unsigned a) {
    if(x>=0&&x<W&&y>=0&&y<H&&a) {
        Color &p=fb[y*pitch+x];p=a==255?c:blend(p,c,a);
    }
}
void line(int x0,int y0,int x1,int y1,Color c) {
    int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    for(;;){pixel(x0,y0,c);if(x0==x1&&y0==y1)break;int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}
}
void box(int x,int y,int w,int h,Color fill=panel,Color edge=border) {
    rect(x,y,w,h,fill);rect(x,y,w,1,edge);rect(x,y+h-1,w,1,edge);
    rect(x,y,1,h,edge);rect(x+w-1,y,1,h,edge);
    // Cut corners and short highlights give the panels their circuit-board shape.
    line(x,y+7,x+7,y,edge);line(x+w-8,y+h-1,x+w-1,y+h-8,edge);
    rect(x+1,y+1,6,2,fill);rect(x+1,y+3,4,2,fill);rect(x+1,y+5,2,2,fill);
}
std::vector<uint32_t> decode(const std::string&s) { return nm::ui_text::displayCodepoints(s); }
std::string encode(const std::vector<uint32_t>&v) {
    std::string s;for(auto cp:v) {if(cp<128)s+=static_cast<char>(cp);else{s+=static_cast<char>(0xc0|(cp>>6));s+=static_cast<char>(0x80|(cp&63));}}return s;
}
Font& font(int size) {
    auto it=fonts.find(size);if(it!=fonts.end())return it->second;
    Font scaled;
    for(unsigned i=0;i<224;++i) {
        auto &g=scaled.glyphs[i];g.w=size;g.h=(34*size+12)/24;g.advance=(advances[i]*size+12)/24;
        g.alpha.resize(g.w*g.h);
        for(int y=0;y<g.h;++y)for(int x=0;x<g.w;++x)
            g.alpha[y*g.w+x]=rawFont.empty()?0:rawFont[i*24*34+(y*34/g.h)*24+x*24/g.w];
    }
    return fonts.emplace(size,std::move(scaled)).first->second;
}
int width(const std::string&s,int size) {
    auto &f=font(size);int w=0;for(auto cp:decode(s))w+=f.glyphs[cp-32].advance;return w;
}
void text(const std::string&s,int x,int y,int size=18,Color c=ink) {
    auto &f=font(size);int offset=(5*size+12)/24;
    for(auto cp:decode(s)) {
        const auto &g=f.glyphs[cp-32];
        for(int yy=0;yy<g.h;++yy) {
            int py=y+yy-offset;if(py<0||py>=H)continue;
            for(int xx=0;xx<g.w;++xx)if(g.alpha[yy*g.w+xx])alphaPixel(x+xx,py,c,g.alpha[yy*g.w+xx]);
        }
        x+=g.advance;
    }
}
std::string fit(const std::string&s,int maxw,int size) {
    if(width(s,size)<=maxw)return s;
    auto chars=decode(s);while(!chars.empty()&&width(encode(chars)+"...",size)>maxw)chars.pop_back();
    return encode(chars)+"...";
}
void fitted(const std::string&s,int x,int y,int maxw,int size=18,Color c=ink) {text(fit(s,maxw,size),x,y,size,c);}
void wrapped(const std::string&s,int x,int y,int maxw,int size,int maxlines,Color c=ink) {
    std::istringstream words(s);std::string word,current;int n=0;
    while(words>>word) {
        std::string trial=current.empty()?word:current+" "+word;
        if(width(trial,size)>maxw&&!current.empty()) {
            fitted(current,x,y+n*(size+9),maxw,size,c);if(++n>=maxlines)return;current=word;
        } else current=trial;
        if(n==maxlines-1) {
            std::string rest;std::getline(words,rest);fitted(current+rest,x,y+n*(size+9),maxw,size,c);return;
        }
    }
    if(!current.empty())fitted(current,x,y+n*(size+9),maxw,size,c);
}
// File paths must remain exact: wrap at character boundaries, never abbreviate.
std::vector<std::string> pathLines(const std::string&s,int maxw,int size) {
    std::vector<std::string> lines;std::vector<uint32_t> current;
    for(auto cp:decode(s)) {
        auto trial=current;trial.push_back(cp);
        if(!current.empty()&&width(encode(trial),size)>maxw){lines.push_back(encode(current));current.clear();}
        current.push_back(cp);
    }
    if(!current.empty())lines.push_back(encode(current));
    return lines;
}
void exactPath(const std::string&s,int x,int y,int maxw,int size,int maxlines,Color c=muted) {
    const auto lines=pathLines(s,maxw,size);
    for(int i=0;i<std::min(maxlines,static_cast<int>(lines.size()));++i)text(lines[i],x,y+i*(size+7),size,c);
    if(static_cast<int>(lines.size())>maxlines)text("Continúa en el resumen de rutas.",x,y+maxlines*(size+7),12,dim);
}
void logoAt(int x,int y,int size) {
    if(logo.size()!=256*256*4){box(x,y,size,size,raised,magenta);text("NM",x+size/8,y+size/4,size/3,cyan);return;}
    for(int yy=0;yy<size;++yy)for(int xx=0;xx<size;++xx) {
        size_t p=((yy*256/size)*256+xx*256/size)*4;
        alphaPixel(x+xx,y+yy,rgb(logo[p],logo[p+1],logo[p+2]),logo[p+3]);
    }
}

void footer(const std::vector<std::pair<std::string,std::string>>&keys) {
    rect(35,659,1210,1,border);int x=39;
    for(const auto&k:keys){box(x,677,29,25,raised,border);text(k.first,x+7,682,13,ink);text(k.second,x+37,682,14,muted);x+=width(k.second,14)+60;}
    text("GUARDIAN / "+std::string(AppVersion),1070,684,12,orange);
}
void accent(int x,int y,int w){const Color colors[]={cyan,green,orange};for(int i=0;i<3;++i)rect(x+i*w/3,y,w/3+1,3,colors[i]);}
void header(const View&v){
    logoAt(36,20,65);text("NINMAGIC",112,26,27);text("GUARDIAN / RESCATE",113,61,12,green);
    rect(363,35,1,35,border);text("TU ARRANQUE, A SALVO",387,43,18,muted);
    box(1020,37,221,30,raised,orange);text("BETA / "+std::string(AppVersion),1042,44,14,orange);accent(36,99,1207);
    fitted("FW "+(v.firmware.empty()?std::string("sin comprobar"):v.firmware),38,116,230,13,muted);
    fitted(v.atmosphere?"Atmosphère confirmado":"Atmosphère no confirmado",277,116,375,13,v.atmosphere?green:orange);
    fitted(v.model,671,116,300,13,cyan);text(v.appletMode?"MODO ÁLBUM":"MODO APLICACIÓN",1022,116,13,muted);
}
void heading(const std::string&a,const std::string&b){text(a,37,157,29);text(b,38,200,17,muted);}
const char* titles[]={"Crear / actualizar respaldo","Comprobar el respaldo","Restaurar el arranque","Preparar rescate en Hekate","Elegir extras del respaldo","Revisar temas y módulos"};
const char* subtitles[]={"Hazlo cuando hayas comprobado que todo funciona bien.","Revisa archivos y huellas de la copia guardada.","Recupera solo los archivos incluidos en tu respaldo.","Acceso independiente para cuando Atmosphère no abra.","El núcleo y la configuración se incluyen automáticamente.","Compara cambios y aparta solo lo que confirmes."};
void home(const View&v){
    heading("Vuelve al arranque que funcionaba","Respaldo selectivo de Atmosphère y Hekate en tu microSD");
    box(37,244,373,380);accent(38,245,371);logoAt(125,261,192);
    text("ÚLTIMO RESPALDO",61,473,13,green);
    text(!v.backup.present?"Sin respaldo":v.backup.restore_pending?"Restauración pendiente":v.backup.verified?"Copia verificada":"Copia por comprobar",61,505,22,v.backup.verified?green:orange);
    fitted(v.backup.present?std::to_string(v.backup.files)+" archivos / "+formatBytes(v.backup.bytes):"Crea la primera copia antes de necesitarla.",61,548,322,15,muted);
    fitted(v.backup.present?v.backup.created:v.environmentDetail,61,582,322,13,muted);
    box(429,244,814,380);accent(430,245,812);
    for(size_t i=0;i<6;++i){int y=262+int(i)*55;bool sel=v.selected==i;box(447,y,777,50,sel?rgb(16,48,64):panel,sel?green:border);text(std::to_string(i+1),463,y+15,22,sel?orange:dim);fitted(titles[i],501,y+8,704,21,sel?ink:muted);fitted(subtitles[i],501,y+33,704,12,muted);}
    footer({{"A","Elegir"},{"R","Actualizar app"},{"-","Ayuda"},{"+","Salir"}});
}
void options(const View&v){
    heading("Extras que puedes incluir","Los archivos esenciales del arranque se incluyen sin tener que marcarlos");
    box(37,244,1206,380);accent(38,245,1204);
    const char*names[]={"Imágenes del menú y fondos de Hekate","Otros payloads de bootloader/payloads"};
    const char*details[]={"Son opcionales. No incluyen las capturas de pantalla.","Son herramientas de arranque, no juegos ni archivos de sus carpetas."};
    for(size_t i=0;i<2;++i){int y=281+int(i)*99;bool on=v.options&(i?GC_OPT_PAYLOADS:GC_OPT_VISUALS);box(62,y,1156,80,raised,v.selected==i?green:border);box(81,y+23,24,24,on?green:panel,on?green:muted);if(on)text("+",86,y+24,16,panel);text(names[i],128,y+12,22);text(details[i],128,y+48,16,muted);}
    wrapped("Siempre quedan fuera los juegos, puertos, partidas, mods, cheats, sistemas Android/Linux y el contenido de la emuMMC. Lo excluido se conserva en la SD.",63,512,1153,19,3,orange);
    footer({{"A","Marcar"},{"B","Guardar y volver"}});
}
void backupConfirm(const View&v){
    heading("Guardar el estado que funciona","Confirma solo después de probar que tu consola arranca y funciona bien");
    box(37,244,354,380);accent(38,245,352);logoAt(115,280,196);
    text("NUEVA COPIA",62,503,13,green);text(formatBytes(v.source.bytes),62,538,30,cyan);text(std::to_string(v.source.files)+" archivos",62,588,16,muted);
    box(410,244,833,380);accent(411,245,831);text("ATMOSPHÈRE + HEKATE + CONFIGURACIÓN",435,273,15,green);
    wrapped("Se copiarán y verificarán los archivos reconocidos del arranque. El respaldo anterior se conserva hasta verificar y promover el nuevo.",435,319,779,23,4);
    wrapped("Necesitas espacio temporal para la copia nueva. Al terminar solo se mantiene un respaldo válido. No se copian juegos ni se modifica la memoria interna.",435,467,779,18,4,muted);
    footer({{"A","Todo funciona bien: crear respaldo"},{"B","Cancelar"}});
}
void restoreConfirm(const View&v){
    heading("Restaurar el último respaldo","Se verificarán las huellas antes de sustituir los archivos incluidos");
    box(37,244,1206,380);accent(38,245,1204);text("COPIA GUARDADA",63,273,13,green);
    text(std::to_string(v.backup.files)+" archivos / "+formatBytes(v.backup.bytes),63,308,28);text("Creada: "+std::string(v.backup.created),63,356,18,muted);
    text("Firmware registrado: "+std::string(v.backup.firmware),63,394,18,cyan);
    wrapped("Los juegos, partidas y carpetas excluidas se conservan. Se recuperan los archivos del respaldo; los añadidos posteriores no se eliminan automáticamente.",63,446,1154,21,3);
    wrapped("Esta copia no deshace una actualización de Nintendo ni crea compatibilidad con un firmware nuevo. Si una restauración se interrumpe, vuelve a esta opción o usa el rescatador.",63,548,1154,18,3,orange);
    footer({{"A","Confirmar restauración"},{"B","Cancelar"}});
}
void rescueConfirm(){
    heading("Preparar rescate en Hekate","Hazlo ahora, antes de necesitar recuperar Atmosphère");
    box(37,244,1206,380);accent(38,245,1204);
    text("HEKATE > MORE CONFIGS > NINMAGIC GUARDIAN",63,273,19,green);
    wrapped("Se comprueban y copian el rescatador y sus recursos independientes, y se añade su acceso en bootloader/ini/ninmagic-guardian.ini.",63,326,1153,23,3);
    wrapped("El rescatador utiliza el mismo respaldo creado por esta aplicación. No necesita que Atmosphère arranque. Hekate sí debe poder abrirlo y la microSD debe estar legible.",63,442,1153,20,3,muted);
    wrapped("No cambia tus entradas Nintendo / Hack Magia. Primera beta: el arranque del rescatador necesita prueba física en cada modelo de consola.",63,551,1153,18,2,orange);
    footer({{"A","Preparar y verificar acceso"},{"B","Cancelar"}});
}

void addonReview(const View&v){
    heading("Posibles cambios del arranque","Compara con el respaldo; un cambio no demuestra la causa del fallo");
    box(37,244,748,380);accent(38,245,746);box(804,244,439,380);accent(805,245,437);
    if(!v.addons.ok||!v.addons.count)wrapped(v.addons.message,63,285,696,21,9,v.addons.ok?muted:orange);
    const unsigned start=unsigned(v.selected/6)*6;
    for(unsigned i=start;i<v.addons.count&&i<start+6;++i){int y=266+int(i-start)*50;const auto&c=v.addons.candidates[i];bool sel=v.selected==i;box(52,y,718,44,sel?raised:panel,sel?green:border);bool marked=i<v.markedAddons.size()&&v.markedAddons[i];box(65,y+13,17,17,marked?green:panel,marked?green:muted);if(marked)text("+",68,y+13,13,panel);fitted(c.kind==GA_HOME_THEME?"Tema del menú HOME":"Módulo de arranque "+std::string(c.title_id),96,y+6,659,17);text(c.newly_added?"Añadido después del respaldo":"Cambió desde el respaldo",97,y+28,12,orange);}
    text("APARTAR DE FORMA REVERSIBLE",827,274,13,green);
    wrapped("Los módulos se desactivan apartando su marcador de inicio. El tema se mueve a una carpeta de recuperación. Los mods de los juegos se conservan.",827,316,390,21,6);
    wrapped(v.addons.quarantine_pending?"Hay un apartado pendiente: Y revisa cómo continuarlo.":"A marca, Y revisa antes de apartar. X permite recuperar lo que apartaste.",827,514,390,17,4,orange);
    footer({{"A","Marcar"},{"Y","Revisar"},{"X","Recuperar apartados"},{"B","Volver"}});
}
void addonConfirm(const View&v,bool reactivate){
    heading(reactivate?"Recuperar complementos apartados":"Confirmar apartado de complementos",reactivate?"No se reemplazará ningún archivo nuevo que ocupe su ruta":"Se vuelven a comprobar los cambios antes de moverlos");
    box(37,244,1206,380);accent(38,245,1204);unsigned count=0;for(bool b:v.markedAddons)if(b)++count;
    text(reactivate?"REACTIVAR LO APARTADO":v.addons.quarantine_pending?"CONTINUAR EL PLAN YA CONFIRMADO":std::to_string(count)+" COMPLEMENTOS SELECCIONADOS",63,273,22,green);
    if(!reactivate){int y=322;unsigned shown=0;for(unsigned i=0;i<v.addons.count&&i<v.markedAddons.size();++i)if(v.markedAddons[i]&&shown++<5){const auto&c=v.addons.candidates[i];text(c.kind==GA_HOME_THEME?"Tema HOME":"Módulo "+std::string(c.title_id),63,y,18);y+=36;}}
    wrapped(reactivate?"Recuperar los complementos también puede devolver el fallo que causaban. Si alguna ruta está ocupada, se conservarán los archivos sin sobrescribirlos.":"Se conservan los archivos para poder recuperarlos desde esta aplicación o desde el rescatador. No se demuestra que estos cambios sean responsables del fallo.",63,516,1153,21,3,orange);
    footer({{"A",reactivate?"Confirmar recuperación":"Confirmar apartado"},{"B","Cancelar"}});
}

void help(){
    heading("Una copia del arranque, sin copiar tus juegos","Primera beta: prueba la copia y el diagnóstico antes de restaurar");
    box(37,244,1206,380);accent(38,245,1204);
    const char*heads[]={"1. PRUEBA Y CREA EL RESPALDO","2. PREPARA EL RESCATE EN HEKATE","3. RECUPERA DESDE LA CONSOLA","4. CONOCE EL LÍMITE"};
    const char*texts[]={"Cuando todo funcione, elige Crear / actualizar respaldo. Los archivos esenciales se seleccionan automáticamente.","El acceso usa botones VOL+/VOL- y POWER. Primero abre el rescatador y comprueba la copia sin restaurar nada.","Si abre Atmosphère, restaura desde Guardian. Si no abre, entra en Hekate > More configs > NinMagic Guardian.","No repara daños físicos, particiones o NAND. No revierte firmware, no garantiza corregir plugins externos y no instala juegos."};
    for(int i=0;i<4;++i){int y=269+i*84;text(heads[i],63,y,14,green);wrapped(texts[i],63,y+28,1155,18,2,muted);}footer({{"B","Volver"},{"+","Salir"}});
}
void update(const View&v) {
    heading("Actualizar NinMagic Guardian", "Canal independiente: ANE-JV / ninmagic-guardian");
    box(37,244,341,380);accent(38,245,339);logoAt(117,274,180);
    text("VERSIÓN EN EJECUCIÓN",61,489,13,cyan);text(AppVersion,61,521,32);
    fitted(v.update.sdVerified?"microSD: "+v.update.sdVersion:"microSD: sin comprobar",61,583,292,16,muted);
    box(396,244,847,380);accent(397,245,845);
    std::string title="Consulta pendiente", body="Pulsa X para consultar la versión publicada. Una consulta fallida no significa que tengas la última versión.";Color color=muted;
    if(v.queryComplete&&v.updateError.empty()&&!v.update.version.empty()) {
        if(v.update.installed||(publishedAppOnSd(v.update)&&v.update.version!=AppVersion)){title="Cierra y vuelve a abrir NinMagic Guardian";body="El NRO publicado ya está en la microSD. La aplicación en memoria seguirá mostrando su versión anterior hasta que la cierres.";color=green;}
        else if(v.update.available){title="Nueva versión: "+v.update.version;body=v.update.prepared?"La descarga ya está verificada. A vuelve a la revisión; todavía no se ha sustituido el NRO instalado.":"A descarga el NRO y comprueba su nombre, versión, tamaño y SHA-256. Después podrás confirmar la instalación.";color=cyan;}
        else {title=v.update.version==AppVersion?"Ya tienes la última versión publicada":"No hay una versión posterior publicada";body="Versión consultada: "+v.update.version+". Puedes volver a comprobar el canal con X.";color=green;}
    }
    if(!v.recoveryError.empty()&&v.updateRecord.state!=nm::CompletedUpdateState::CanArchive){title="La instalación necesita revisión";body=v.recoveryError;color=orange;}
    if(v.updateRecord.state==nm::CompletedUpdateState::Blocked){title="La instalación necesita revisión";body=v.updateRecord.detail;color=orange;}
    if(!v.updateError.empty()){title="No se pudo completar la consulta";body=v.updateError;color=orange;}
    text("CANAL HTTPS",422,270,13,magenta);wrapped(title,422,303,796,24,2,color);wrapped(body,422,385,796,18,5,muted);
    if(v.updateRecord.state==nm::CompletedUpdateState::CanArchive)fitted("Y: revisar un registro de actualización finalizado",422,555,793,16,cyan);
    else if(!v.recoveryError.empty())fitted("Registro: "+v.recoveryError,422,555,793,14,orange);
    std::vector<std::pair<std::string,std::string>> keys;
    if(canDownloadAppUpdate(v)){keys.push_back({"A",v.update.prepared?"Revisar descarga":"Descargar"});text("La instalación se confirma en una pantalla de revisión.",422,595,15,muted);}
    keys.push_back({"X","Comprobar"});
    if(v.updateRecord.state==nm::CompletedUpdateState::CanArchive)keys.push_back({"Y","Registro"});
    keys.push_back({"B","Volver"});keys.push_back({"+","Salir"});footer(keys);
}
void confirmation(const View&v,bool archive) {
    heading(archive?"Archivar registro finalizado":"Instalar la actualización", "Confirma solamente después de revisar estos datos");
    box(37,244,1206,380);accent(38,245,1204);
    text(archive?"REGISTRO COMPROBADO":"DESCARGA COMPROBADA",63,272,14,cyan);
    wrapped(archive?v.updateRecord.detail:"NinMagic Guardian "+v.update.version+" reemplazará la copia instalada. Se conserva un NRO anterior. El rescatador de Hekate se actualiza con el ZIP completo.",63,310,1156,23,3);
    text(archive?"Se conservan el NRO y sus copias de seguridad.":"Después de instalar, cierra la aplicación y vuelve a abrirla.",63,426,19,magenta);
    if(!archive){text("SHA-256",63,471,13,muted);fitted(v.update.sha256,63,498,1156,17,cyan);exactPath(AppPath,63,543,1156,17,2,muted);}
    else wrapped("Solo se archivarán los registros TXT revisados. No se modificarán el respaldo ni el rescatador.",63,480,1156,18,3,muted);
    footer({{"A",archive?"Confirmar archivo":"Confirmar instalación"},{"B","Cancelar"}});
}

void busy(const View&v){
    heading("Operación en curso","Espera al resultado antes de cerrar la aplicación");box(37,244,1206,380);accent(38,245,1204);logoAt(73,295,170);
    wrapped(v.operation,280,290,915,27,3);fitted(v.progressPath,282,415,914,16,cyan);wrapped("No extraigas la microSD ni apagues la consola durante la operación.",282,468,914,20,2,muted);
    rect(282,565,914,8,border);if(v.total){double p=std::min(1.0,double(v.current)/double(v.total));rect(282,565,int(914*p),8,green);text(std::to_string(int(p*100))+" % de este paso",282,529,17,green);}else {int step=int(v.seconds*210)%760;rect(282+step,565,154,8,green);}
    footer({});
}
void result(const View&v){heading(v.success?"Operación completada":"Revisa el resultado",v.success?"Los detalles indican qué se ha comprobado":"No continúes con una copia sin verificar");box(37,244,1206,380);accent(38,245,1204);logoAt(69,300,180);wrapped(v.message,280,284,916,22,10,v.success?ink:orange);footer({{"B",v.restartRequired?"Cerrar Guardian":"Volver"},{"+","Salir"}});}
uint32_t le32(const uint8_t*p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
std::vector<uint8_t> readFile(const std::string&path){std::ifstream in(path,std::ios::binary);if(!in)return{};in.seekg(0,std::ios::end);auto n=in.tellg();if(n<0||n>4000000)return{};in.seekg(0);std::vector<uint8_t>d(static_cast<size_t>(n));in.read(reinterpret_cast<char*>(d.data()),n);return in?d:std::vector<uint8_t>{};}
}
std::string formatBytes(uint64_t b){char s[64];std::snprintf(s,sizeof(s),b>=1073741824?"%.2f GiB":"%.1f MiB",double(b)/(b>=1073741824?1073741824.0:1048576.0));return s;}
bool uiInit(const std::string&assetRoot) {
    uiExit();auto data=readFile(assetRoot+"/font.bin");logo=readFile(assetRoot+"/logo.rgba");
    if(data.size()!=32+224+224*24*34||le32(data.data())!=0x54464D4E||le32(data.data()+4)!=32||le32(data.data()+8)!=224||le32(data.data()+12)!=24||le32(data.data()+16)!=34)return false;
    std::copy(data.begin()+32,data.begin()+256,advances.begin());rawFont.assign(data.begin()+256,data.end());
    for(int size:{12,13,14,15,16,17,18,19,20,22,23,24,25,26,27,29,32})font(size);
    background.resize(W*H);Color *oldfb=fb;int oldpitch=pitch;fb=background.data();pitch=W;
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){unsigned glow=static_cast<unsigned>(std::max(0,290-(std::abs(x-1100)+std::abs(y-120))/2));pixel(x,y,rgb(6+glow/35,15+glow/16,25+glow/11));}
    Color trace=rgb(20,51,66),node=rgb(65,106,65);
    for(int i=0;i<5;++i){int x=710+i*106;line(x,0,x,24+i*11,trace);line(x,24+i*11,x+22,46+i*11,trace);line(x+22,46+i*11,1279,46+i*11,trace);rect(x-2,22+i*11,5,5,node);}
    fb=oldfb;pitch=oldpitch;return true;
}

void uiDraw(uint32_t*pixels,int stride,const View&v){if(!pixels||stride<W)return;fb=pixels;pitch=stride;if(background.size()==W*H)for(int y=0;y<H;++y)std::memcpy(fb+y*pitch,background.data()+y*W,W*sizeof(Color));else rect(0,0,W,H,rgb(6,15,25));header(v);switch(v.screen){
case Screen::Home:home(v);break;case Screen::Options:options(v);break;case Screen::BackupConfirm:backupConfirm(v);break;case Screen::RestoreConfirm:restoreConfirm(v);break;case Screen::RescueConfirm:rescueConfirm();break;case Screen::AddonReview:addonReview(v);break;case Screen::QuarantineConfirm:addonConfirm(v,false);break;case Screen::ReactivateConfirm:addonConfirm(v,true);break;case Screen::Help:help();break;case Screen::Update:update(v);break;case Screen::ConfirmUpdate:confirmation(v,false);break;case Screen::ConfirmArchive:confirmation(v,true);break;case Screen::Busy:busy(v);break;case Screen::Result:result(v);break;}}
void uiExit(){fonts.clear();rawFont.clear();logo.clear();background.clear();fb=nullptr;pitch=W;}
}
