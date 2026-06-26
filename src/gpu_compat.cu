// gpu_compat.cu — Filter2_0A/0B compiled with compute_89 PTX (JIT to sm_120)
// Octave-0 prefilter: evaluate octave 0 first, skip octave 1 unless prefilter passes
#include "gpu_types.h"
#include "common.h"
#include <cstdio>
#define TRY_CUDA(e) do{cudaError_t _e=(e);if(_e!=cudaSuccess){fprintf(stderr,"CUDA %s:%d %s\n",__FILE__,__LINE__,cudaGetErrorString(_e));abort();}}while(0)

static constexpr int32_t lbpm = large_biomes ? 4 : 1;
static constexpr int32_t pos_step = 8192 * lbpm / 4 / 16;
static constexpr int32_t pos_offset = -(pos_step * 15 / 2);

constexpr double _root_vf = 0.16666666666666666 / (0.1 * (1.0 + 1.0 / 9.0));
constexpr int32_t _first_octave = large_biomes ? -11 : -9;
constexpr double _base_if = 1.0 / (double)(1 << (-_first_octave));
constexpr double _base_vf = (double)(1 << 8) / ((double)(1 << 9) - 1.0) * _root_vf;
constexpr double _b_mult = 1.0181268882175227;

static constexpr float if0_a = (float)(_base_if);
static constexpr float if1_a = (float)(_base_if * 2.0);
static constexpr float if0_b = (float)(_base_if * _b_mult);
static constexpr float if1_b = (float)(_base_if * 2.0 * _b_mult);
static constexpr float vf0 = (float)(_base_vf);
static constexpr float vf1 = (float)(_base_vf * 0.5);
static constexpr float noise_threshold = -0.55f;
static constexpr float kPrefilterThreshold = -0.45f;

__forceinline__ __device__ float smoothstep(float v){return v*v*v*(v*(v*6.f-15.f)+10.f);}
__forceinline__ __device__ float lerp1(float fx,float v0,float v1){return fmaf(fx,v1-v0,v0);}
__forceinline__ __device__ float lerp2(float fx,float fy,float v00,float v10,float v01,float v11){return lerp1(fy,lerp1(fx,v00,v10),lerp1(fx,v01,v11));}
__forceinline__ __device__ float lerp3(float fx,float fy,float fz,float a,float b,float c,float d,float e,float f,float g,float h){
    return lerp1(fz,lerp2(fx,fy,a,b,c,d),lerp2(fx,fy,e,f,g,h));}
__forceinline__ __device__ float gradDot(const GradDotTable&t,uint8_t p,float x,float y,float z){
    uint32_t h=p&0xF;return fmaf(x,t.x[h],fmaf(y,t.y[h],z*t.z[h]));}
__device__ __inline__ uint32_t warp_reduce_add(uint32_t v) {
    #pragma unroll
    for(int o=16;o>0;o/=2) v+=__shfl_xor_sync(0xFFFFFFFF,v,o);
    return v;
}
__device__ __inline__ void compute_cell(const ImprovedNoise&n,int32_t ix,int32_t iy,int32_t iz,
    uint8_t&c000,uint8_t&c100,uint8_t&c010,uint8_t&c110,uint8_t&c001,uint8_t&c101,uint8_t&c011,uint8_t&c111){
    uint8_t p0=n.p[ix&0xFF],p1=n.p[(ix+1)&0xFF];
    uint8_t p00=n.p[(p0+iy)&0xFF],p01=n.p[(p0+iy+1)&0xFF],p10=n.p[(p1+iy)&0xFF],p11=n.p[(p1+iy+1)&0xFF];
    c000=n.p[(p00+iz)&0xFF];c100=n.p[(p10+iz)&0xFF];c010=n.p[(p01+iz)&0xFF];c110=n.p[(p11+iz)&0xFF];
    c001=n.p[(p00+iz+1)&0xFF];c101=n.p[(p10+iz+1)&0xFF];c011=n.p[(p01+iz+1)&0xFF];c111=n.p[(p11+iz+1)&0xFF];
}
__device__ __inline__ float interp(const GradDotTable&t,float fx_,float fy_,float fz_,float fx,float fy,float fz,
    uint8_t c000,uint8_t c100,uint8_t c010,uint8_t c110,uint8_t c001,uint8_t c101,uint8_t c011,uint8_t c111){
    float n000=gradDot(t,c000,fx_,fy_,fz_),n100=gradDot(t,c100,fx_-1.f,fy_,fz_);
    float n010=gradDot(t,c010,fx_,fy_-1.f,fz_),n110=gradDot(t,c110,fx_-1.f,fy_-1.f,fz_);
    float n001=gradDot(t,c001,fx_,fy_,fz_-1.f),n101=gradDot(t,c101,fx_-1.f,fy_,fz_-1.f);
    float n011=gradDot(t,c011,fx_,fy_-1.f,fz_-1.f),n111=gradDot(t,c111,fx_-1.f,fy_-1.f,fz_-1.f);
    return lerp3(fx,fy,fz,n000,n100,n010,n110,n001,n101,n011,n111);
}

void init_compat_tables() {}

template<uint32_t MinCount, bool IsB>
__global__ __launch_bounds__(256) void filter2_compat_kernel(
    InputBuffer<SeedPos> inputs, OutputBuffer<SeedPos> outputs,
    KernelSeed1::Result *results, GradDotTable gdt)
{
    constexpr float if0 = IsB ? if0_b : if0_a;
    constexpr float if1 = IsB ? if1_b : if1_a;
    __shared__ GradDotTable s_gdt;
    __shared__ ImprovedNoise s_oct0[8], s_oct1[8];
    if(threadIdx.x==0) s_gdt=gdt;
    __syncthreads();
    const uint32_t lane=threadIdx.x&31u, warp=threadIdx.x>>5;
    const uint32_t wg=(blockIdx.x*blockDim.x+threadIdx.x)>>5, nw=(gridDim.x*blockDim.x)>>5;
    const uint32_t z_index=lane>>1, x_start=(lane&1u)*8u;
    constexpr uint32_t words=sizeof(ImprovedNoise)/sizeof(uint32_t);
    ImprovedNoise &oct0=s_oct0[warp], &oct1=s_oct1[warp];
    for(uint32_t idx=wg; idx<*inputs.len; idx+=nw) {
        SeedPos input=inputs.data[idx];
        uint32_t seed_index=input.seed_index;
        const uint32_t *src0, *src1;
        if constexpr(IsB) {
            src0=(const uint32_t*)&results[seed_index].continentalness_0B;
            src1=(const uint32_t*)&results[seed_index].continentalness_1B;
        } else {
            src0=(const uint32_t*)&results[seed_index].continentalness_0A;
            src1=(const uint32_t*)&results[seed_index].continentalness_1A;
        }
        uint32_t*d0=(uint32_t*)&oct0,*d1=(uint32_t*)&oct1;
        for(uint32_t i=lane;i<words;i+=32){d0[i]=src0[i];d1[i]=src1[i];}
        __syncwarp();

        int32_t z_world=input.z+(int32_t)(z_index*pos_step)+pos_offset;
        int32_t x_base=input.x+(int32_t)(x_start*pos_step)+pos_offset;

        // Phase 1: Octave 0 only — evaluate all 8 x-steps, store results
        float y0=oct0.yo;int32_t iy0=__float2int_rd(y0);float fy0_=y0-(float)iy0;float fy0=smoothstep(fy0_);
        float z0c=z_world*if0+oct0.zo;int32_t iz0=__float2int_rd(z0c);float fz0_=z0c-(float)iz0;float fz0=smoothstep(fz0_);

        int32_t cur_ix0=0;bool have0=false;
        uint8_t a000,a100,a010,a110,a001,a101,a011,a111;
        float noise0_vals[8];
        uint32_t prefilter_count=0;

        #pragma unroll
        for(uint32_t k=0;k<8;k++){
            int32_t xw=x_base+(int32_t)(k*pos_step);
            float x0c=xw*if0+oct0.xo;int32_t ix0=__float2int_rd(x0c);float fx0_=x0c-(float)ix0;
            if(!have0||ix0!=cur_ix0){compute_cell(oct0,ix0,iy0,iz0,a000,a100,a010,a110,a001,a101,a011,a111);cur_ix0=ix0;have0=true;}
            float fx0=smoothstep(fx0_);
            float n0=interp(s_gdt,fx0_,fy0_,fz0_,fx0,fy0,fz0,a000,a100,a010,a110,a001,a101,a011,a111);
            noise0_vals[k]=n0*vf0;
            prefilter_count+=(noise0_vals[k]<kPrefilterThreshold)?1u:0u;
        }

        uint32_t prefilter_total=warp_reduce_add(prefilter_count);

        // Phase 2: Only evaluate octave 1 if octave 0 prefilter passes
        if(prefilter_total>=MinCount){
            float y1=oct1.yo;int32_t iy1=__float2int_rd(y1);float fy1_=y1-(float)iy1;float fy1=smoothstep(fy1_);
            float z1c=z_world*if1+oct1.zo;int32_t iz1=__float2int_rd(z1c);float fz1_=z1c-(float)iz1;float fz1=smoothstep(fz1_);
            int32_t cur_ix1=0;bool have1=false;
            uint8_t b000,b100,b010,b110,b001,b101,b011,b111;
            uint32_t local_count=0;

            #pragma unroll
            for(uint32_t k=0;k<8;k++){
                int32_t xw=x_base+(int32_t)(k*pos_step);
                float x1c=xw*if1+oct1.xo;int32_t ix1=__float2int_rd(x1c);float fx1_=x1c-(float)ix1;
                if(!have1||ix1!=cur_ix1){compute_cell(oct1,ix1,iy1,iz1,b000,b100,b010,b110,b001,b101,b011,b111);cur_ix1=ix1;have1=true;}
                float fx1=smoothstep(fx1_);
                float n1=interp(s_gdt,fx1_,fy1_,fz1_,fx1,fy1,fz1,b000,b100,b010,b110,b001,b101,b011,b111);
                float val=noise0_vals[k]+n1*vf1;
                local_count+=(val<noise_threshold)?1u:0u;
            }

            uint32_t total=warp_reduce_add(local_count);
            if(lane==0&&total>=MinCount){uint32_t ri=atomicAdd(outputs.len,1);if(ri<outputs.max_len)outputs.data[ri]={seed_index,input.x,input.z};}
        }
        __syncwarp();
    }
}

static GradDotTable make_gdt(){
    GradDotTable t;
    t.x[0]=1;t.y[0]=1;t.z[0]=0; t.x[1]=-1;t.y[1]=1;t.z[1]=0;
    t.x[2]=1;t.y[2]=-1;t.z[2]=0; t.x[3]=-1;t.y[3]=-1;t.z[3]=0;
    t.x[4]=1;t.y[4]=0;t.z[4]=1; t.x[5]=-1;t.y[5]=0;t.z[5]=1;
    t.x[6]=1;t.y[6]=0;t.z[6]=-1; t.x[7]=-1;t.y[7]=0;t.z[7]=-1;
    t.x[8]=0;t.y[8]=1;t.z[8]=1; t.x[9]=0;t.y[9]=-1;t.z[9]=1;
    t.x[10]=0;t.y[10]=1;t.z[10]=-1; t.x[11]=0;t.y[11]=-1;t.z[11]=-1;
    t.x[12]=1;t.y[12]=1;t.z[12]=0; t.x[13]=0;t.y[13]=-1;t.z[13]=1;
    t.x[14]=-1;t.y[14]=1;t.z[14]=0; t.x[15]=0;t.y[15]=-1;t.z[15]=-1;
    return t;
}

namespace KernelFilter2_0A {
    void run(InputBuffer<SeedPos> inputs, OutputBuffer<SeedPos> outputs,
             KernelSeed1::Result *results, cudaStream_t stream) {
        filter2_compat_kernel<27,false><<<2380,256,0,stream>>>(inputs,outputs,results,make_gdt());
        TRY_CUDA(cudaGetLastError());
    }
}
namespace KernelFilter2_0B {
    void run(InputBuffer<SeedPos> inputs, OutputBuffer<SeedPos> outputs,
             KernelSeed1::Result *results, cudaStream_t stream) {
        filter2_compat_kernel<20,true><<<2380,256,0,stream>>>(inputs,outputs,results,make_gdt());
        TRY_CUDA(cudaGetLastError());
    }
}
