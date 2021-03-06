extern "C" void fdtd(int nx,int ny,int nz,
		float* Hx,float* Hy,float* Hz,
		float* Ex,float* Ey,float* Ez, float Cbdy, float Cbdz, float Cbdx,
		float* Ey1, float* Ex1, float* Ez1, float* Ez2);
void fdtd(int nx,int ny,int nz,
		float* Hx,float* Hy,float* Hz,
		float* Ex,float* Ey,float* Ez, float Cbdy, float Cbdz, float Cbdx,
		float* Ey1, float* Ex1, float* Ez1, float* Ez2)
{
    int k, j, i;
    float Eycur;
    float Excur;
    float Hxin;
    float Hyin;
    float Hzin;
    float Eyin;
    float Eyin_kp1;
    float Eyin_ip1;
    float Ezin;
    float Ezinj_p1;
    float Ezini_p1;
    
    float Exin;
    float Exin_jp1;
    float Exin_kp1;
    
    
    int r3_fp_0xc;      //j
    int r3_fp_0x8;      //k
    int r3_fp_0x10 = 1; //i
    lab100a88:
        if(r3_fp_0x10 > nx)
            goto lab100a98; 
        lab1005ac:
            r3_fp_0xc = 1;            
        lab100a6c:
            if(r3_fp_0xc > ny)
                goto lab100a7c;                
            lab1005b8:
                Eycur =Ey[r3_fp_0x10*nz*ny+r3_fp_0xc*nz+1];
        	    Excur = Ex[r3_fp_0x10*nz*ny+r3_fp_0xc*nz+1]; 
        	    r3_fp_0x8 = 1;
        	    lab100a50:  
        	        if(r3_fp_0x8 > nz)
        	            goto lab100a60;
        	        lab10063c:
        	            Hxin =  Hx[r3_fp_0x10*nz*ny+r3_fp_0xc*nz+r3_fp_0x8];
                    	Hyin =  Hy[r3_fp_0x10*nz*ny+r3_fp_0xc*nz+r3_fp_0x8];
                    	Hzin =  Hz[r3_fp_0x10*nz*ny+r3_fp_0xc*nz+r3_fp_0x8];

                    	Eyin = Eycur;
                    	Eyin_kp1 = Ey[r3_fp_0x10*nz*ny+nz*r3_fp_0xc+r3_fp_0x8+1];
                    	Eycur = Eyin_kp1;
                    	Eyin_ip1 = Ey1[(r3_fp_0x10+1)*nz*ny+r3_fp_0xc*nz+r3_fp_0x8];

                    	Ezin = Ez[r3_fp_0x10*nz*ny+r3_fp_0xc*nz+r3_fp_0x8];
                    	Ezinj_p1 = Ez1[r3_fp_0x10*nz*ny+(r3_fp_0xc+1)*nz+r3_fp_0x8];
                    	Ezini_p1 = Ez2[(r3_fp_0x10+1)*nz*ny+r3_fp_0xc*nz+r3_fp_0x8];

                    	Exin = Excur;
                    	Exin_jp1 = Ex1[r3_fp_0x10*nz*ny+(r3_fp_0xc+1)*nz+r3_fp_0x8];
                    	Exin_kp1 = Ex[r3_fp_0x10*nz*ny+r3_fp_0xc*nz+r3_fp_0x8+1];
                    	Excur = Exin_kp1;

                        Hx[r3_fp_0x10*nz*ny+r3_fp_0xc*nz+r3_fp_0x8] = Hxin + ((Eyin_kp1-Eyin)*Cbdz + (Ezin-Ezinj_p1)*Cbdy);
                        Hy[r3_fp_0x10*nz*ny+r3_fp_0xc*nz+r3_fp_0x8] = Hyin + ((Ezini_p1-Ezin)*Cbdx + (Exin-Exin_kp1)*Cbdz);
                        Hz[r3_fp_0x10*nz*ny+r3_fp_0xc*nz+r3_fp_0x8] = Hzin + ((Exin_jp1-Exin)*Cbdy + (Eyin-Eyin_ip1)*Cbdx);
                        r3_fp_0x8 += 1;
                        goto lab100a50;
            lab100a60:
                r3_fp_0xc+=1;
                goto lab100a6c;
        lab100a7c:
            r3_fp_0x10 += 1;
            goto lab100a88;
    lab100a98:
        return;                                
}
