// Independent contract CPU oracle for white-seeded first update, bottom-left origin.
#include <cmath>
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    int loops=std::atoi(argv[1]);float time=std::strtof(argv[2],nullptr);
    if((loops!=2 && loops!=32) || !std::isfinite(time))return 2;
    for(int y=0;y<600;++y)for(int x=0;x<800;++x){
        float u=(float(x)+.5f)/800.f,w=(float(y)+.5f)/600.f;
        float v=u*12.9898f+w*78.233f+time*.7f;
        for(int i=0;i<loops;++i)v=std::sin(v*1.31f+float(i)*.17f)+std::cos(v*.73f-float(i)*.11f);
        float seed[3]={.5f+.5f*std::sin(v),.5f+.5f*std::cos(v+u*6.283185f),.5f+.5f*std::sin(v+w*9.424778f)};
        unsigned char rgba[4];for(int c=0;c<3;++c)rgba[c]=static_cast<unsigned char>(std::lround(255.f*(.94f+.06f*seed[c])));rgba[3]=255;
        if(std::fwrite(rgba,1,4,stdout)!=4)return 3;
    }
    return 0;
}
