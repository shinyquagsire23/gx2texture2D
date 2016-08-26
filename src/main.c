#include <coreinit/core.h>
#include <coreinit/debug.h>
#include <coreinit/thread.h>
#include <coreinit/foreground.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>

#include <gx2/draw.h>
#include <gx2/enum.h>
#include <gx2/swap.h>
#include <gx2/clear.h>
#include <gx2/state.h>
#include <gx2/texture.h>
#include <gx2/display.h>
#include <gx2/context.h>
#include <gx2/shaders.h>
#include <gx2/registers.h>

#include <vpad/input.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>

#include "memory.h"
#include "model.h"
#include "gl-matrix.h"
#include "gx2_ext.h"

bool is_app_running = true;

#define GX2_INVALIDATE_ATTRIBUTE_BUFFER                 0x00000001
#define GX2_INVALIDATE_TEXTURE                          0x00000002
#define GX2_INVALIDATE_UNIFORM_BLOCK                    0x00000004
#define GX2_INVALIDATE_SHADER                           0x00000008
#define GX2_INVALIDATE_COLOR_BUFFER                     0x00000010
#define GX2_INVALIDATE_DEPTH_BUFFER                     0x00000020
#define GX2_INVALIDATE_CPU                              0x00000040
#define GX2_INVALIDATE_STREAM_OUT_BUFFER                0x00000080
#define GX2_INVALIDATE_EXPORT_BUFFER                    0x00000100

#define TARGET_WIDTH (1920)
#define TARGET_HEIGHT (1080)

float box_x = 512.0f;

static GX2VertexShader *vertexShader = NULL;
static GX2PixelShader *pixelShader = NULL;
static GX2FetchShader *fetchShader = NULL;
static GX2AttribStream *attributes = NULL;
void *fetchShaderProgramm;

GX2Texture texture;

unsigned char *gx2CommandBuffer;
unsigned char *tvScanBuffer;
unsigned char *drcScanBuffer;
GX2ContextState *tvContextState;
GX2ContextState *drcContextState;
GX2ColorBuffer tvColorBuffer;
GX2DepthBuffer tvDepthBuffer;
GX2ColorBuffer drcColorBuffer;
GX2DepthBuffer drcDepthBuffer;

static mat4_t projectionMtx;

static f32 degreeX = 0.0f;
static f32 degreeY = 0.0f;
static f32 degreeZ = 0.0f;
static bool manualControl = false;

bool initialized = false;

void *texture2DData;

//TODO: Add these to wut
void (*GX2Flush)(void);
void (*GX2Invalidate)(int buf_type, void *addr, int length);
void (*GX2CalcColorBufferAuxInfo)(GX2ColorBuffer *colorBuffer, u32 *size, u32 *align);

bool scene_setup = false;
static void setup_scene(void)
{
    if(scene_setup) return;
    
    //TODO: Better .gsh loading, all offsets and amounts are hardcoded to texture2D.gsh here
    
    vertexShader = (GX2VertexShader*) memalign(0x40, sizeof(GX2VertexShader));
    memset(vertexShader, 0, sizeof(GX2VertexShader));
    vertexShader->mode = GX2_SHADER_MODE_UNIFORM_REGISTER;
    vertexShader->size = 0x138;
    vertexShader->program = memalign(0x100, vertexShader->size);
    memcpy(vertexShader->program, (u8*)(texture2DData+0x24C), vertexShader->size);
    GX2Invalidate(GX2_INVALIDATE_CPU | GX2_INVALIDATE_SHADER, vertexShader->program, vertexShader->size);
    memcpy(&vertexShader->regs, (u8*)(texture2DData+0x40), sizeof(vertexShader->regs));

    vertexShader->uniformVarCount = 1;
    vertexShader->uniformVars = (GX2UniformVar*) malloc(vertexShader->uniformVarCount * sizeof(GX2UniformVar));
    vertexShader->uniformVars[0].name = "u_offset";
    vertexShader->uniformVars[0].type = GX2_SHADER_VAR_TYPE_MATRIX4X4 ;
    vertexShader->uniformVars[0].count = 1;
    vertexShader->uniformVars[0].offset = 0;
    vertexShader->uniformVars[0].block = -1;

    vertexShader->attribVarCount = 2;
    vertexShader->attribVars = (GX2AttribVar*) malloc(vertexShader->attribVarCount * sizeof(GX2AttribVar));
    vertexShader->attribVars[0].name = "a_texCoord";
    vertexShader->attribVars[0].type = GX2_SHADER_VAR_TYPE_FLOAT2;
    vertexShader->attribVars[0].count = 0;
    vertexShader->attribVars[0].location = 1;
    vertexShader->attribVars[1].name = "a_position";
    vertexShader->attribVars[1].type = GX2_SHADER_VAR_TYPE_FLOAT3;
    vertexShader->attribVars[1].count = 0;
    vertexShader->attribVars[1].location = 0;

    //Pixel shader setup
    pixelShader = (GX2PixelShader*) memalign(0x40, sizeof(GX2PixelShader));
    memset(pixelShader, 0, sizeof(GX2PixelShader));
    pixelShader->mode = GX2_SHADER_MODE_UNIFORM_REGISTER;
    pixelShader->size = 0x90;
    pixelShader->program = memalign(0x100, pixelShader->size);
    memcpy(pixelShader->program, (u8*)(texture2DData+0x518), pixelShader->size);
    GX2Invalidate(GX2_INVALIDATE_CPU | GX2_INVALIDATE_SHADER, pixelShader->program, pixelShader->size);
    memcpy(&pixelShader->regs, (u8*)(texture2DData+0x3A4), sizeof(pixelShader->regs));

    //Attributes
    attributes = (GX2AttribStream*) malloc(sizeof(GX2AttribStream) * 2);
    GX2InitAttribStream(&attributes[0], vertexShader->attribVars[1].location, 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32);
    GX2InitAttribStream(&attributes[1], vertexShader->attribVars[0].location, 1, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);

    //Fetch Shader
    u32 shaderSize = GX2CalcFetchShaderSizeEx(2, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    fetchShaderProgramm = memalign(0x100, shaderSize);
    fetchShader = (GX2FetchShader *) malloc(sizeof(GX2FetchShader));
    GX2InitFetchShaderEx(fetchShader, fetchShaderProgramm, 2, attributes, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_CPU | GX2_INVALIDATE_SHADER, fetchShaderProgramm, shaderSize);
    
    //Our one texture
    GX2InitTexture(&texture, 128, 128, 1, 0, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_SURFACE_DIM_TEXTURE_2D, GX2_TILE_MODE_LINEAR_ALIGNED);
    texture.surface.image = memalign(texture.surface.alignment, texture.surface.imageSize);
    memcpy(texture.surface.image, test_image, texture.surface.imageSize);
    GX2Invalidate(GX2_INVALIDATE_CPU | GX2_INVALIDATE_TEXTURE, texture.surface.image, texture.surface.imageSize);
    
    scene_setup = true;
}

void takedown_scene()
{
    if(!scene_setup) return;
    
    free(vertexShader->attribVars);
    free(vertexShader->uniformVars);
    free(vertexShader->program);
    free(vertexShader);
    
    free(pixelShader->program);
    free(pixelShader);
    
    free(attributes);
    
    free(fetchShaderProgramm);
    free(fetchShader);
    
    scene_setup = false;
}

float **tex_allocs = NULL;
int num_allocs = 0;

void clean_tex_allocs()
{
    if(!tex_allocs)
    {
        tex_allocs = calloc(128, sizeof(float*));
        return;
    }
    
    for(int i = 0; i < num_allocs; i++)
    {
        free(tex_allocs[i]);
    }
    
    num_allocs = 0;
}

void render_texture(GX2Texture *render_texture, float x_pos, float y_pos, float width, float height)
{
    GX2SetFetchShader(fetchShader);
    GX2SetVertexShader(vertexShader);
    GX2SetPixelShader(pixelShader);
    
    GX2SetPixelTexture(render_texture, 0);
    
    //Assumes that the coordinate space is 1920x1080 with 0,0 in the bottom left corner
    float transform_x = ((x_pos / (float)TARGET_WIDTH) * 2.0f)-1.0f;
    float transform_y = ((y_pos / (float)TARGET_HEIGHT) * 2.0f)-1.0f;
    float transform_width = (width / (float)TARGET_WIDTH) * 2.0f;
    float transform_height = (height / (float)TARGET_HEIGHT) * 2.0f;
    
    float g_tex_buffer_data_temp[] =
    {
         0.0f, 1.0f,
         0.0f, 0.0f,
         1.0f, 1.0f,
         
         1.0f, 0.0f,
         1.0f, 0.0f,
         0.0f, 1.0f,
    };

    float g_vertex_buffer_data_temp[] =
    {
         transform_x, transform_y, 0.0f,
         transform_x, transform_y+transform_height, 0.0f,
         transform_x+transform_width, transform_y, 0.0f,
         
         transform_x+transform_width, transform_y+transform_height, 0.0f,
         transform_x+transform_width, transform_y, 0.0f,
         transform_x, transform_y+transform_height, 0.0f,
    };
    
    float *g_tex_buffer_data = malloc(sizeof(g_tex_buffer_data_temp));
    float *g_vertex_buffer_data = malloc(sizeof(g_vertex_buffer_data_temp));
    memcpy(g_tex_buffer_data, g_tex_buffer_data_temp, sizeof(g_tex_buffer_data_temp));
    memcpy(g_vertex_buffer_data, g_vertex_buffer_data_temp, sizeof(g_vertex_buffer_data_temp));
    
    GX2Invalidate(GX2_INVALIDATE_CPU, g_tex_buffer_data, sizeof(g_tex_buffer_data_temp));
    GX2Invalidate(GX2_INVALIDATE_CPU, g_vertex_buffer_data, sizeof(g_vertex_buffer_data_temp));
    
    tex_allocs[num_allocs++] = g_tex_buffer_data;
    tex_allocs[num_allocs++] = g_vertex_buffer_data;
    
    unsigned int vtxCount = sizeof(g_vertex_buffer_data_temp) / (sizeof(float) * 3);
    
    GX2SetAttribBuffer(1, sizeof(g_tex_buffer_data_temp), sizeof(f32) * 2, g_tex_buffer_data);
    GX2SetAttribBuffer(0, sizeof(g_vertex_buffer_data_temp), sizeof(f32) * 3, g_vertex_buffer_data);
    GX2SetVertexUniformReg(vertexShader->uniformVars[0].offset, 16, projectionMtx);

    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, vtxCount, 0, 1);
}

bool mem1_freed = false;
void free_MEM1_buffers()
{
    if(mem1_freed) return;
    
    MEM2_free(gx2CommandBuffer);
    MEM2_free(tvContextState);
    MEM2_free(drcContextState);
    
    if(tvColorBuffer.surface.aa)
        MEM2_free(tvColorBuffer.aaBuffer);

    if(drcColorBuffer.surface.aa)
        MEM2_free(drcColorBuffer.aaBuffer);

    MEMBucket_free(tvScanBuffer);
    MEMBucket_free(drcScanBuffer);
    
    MEM1_free(tvColorBuffer.surface.image);
    MEM1_free(tvDepthBuffer.surface.image);
    MEM1_free(tvDepthBuffer.hiZPtr);
    MEM1_free(drcColorBuffer.surface.image);
    MEM1_free(drcDepthBuffer.surface.image);
    MEM1_free(drcDepthBuffer.hiZPtr);
    mem1_freed = true;
    
    memoryRelease();
}

bool gx2_killed = false;
void kill_GX2()
{
    if(gx2_killed) return;
    
    GX2SetTVEnable(FALSE);
    GX2SetDRCEnable(FALSE);
    GX2WaitForVsync();
    GX2DrawDone();
    GX2Shutdown();
    gx2_killed = true;
}

void SaveCallback()
{
   OSSavesDone_ReadyToRelease(); // Required
}

bool app_running()
{
   if(!OSIsMainCore())
   {
      ProcUISubProcessMessages(true);
   }
   else
   {
      ProcUIStatus status = ProcUIProcessMessages(true);
    
      if(status == PROCUI_STATUS_EXITING)
      {
          // Being closed, deinit, free, and prepare to exit
          kill_GX2();
          takedown_scene();
          free_MEM1_buffers();
          
          initialized = false;
          is_app_running = false;
          
          ProcUIShutdown();
      }
      else if(status == PROCUI_STATUS_RELEASE_FOREGROUND)
      {
          // Free up MEM1 to next foreground app, deinit screen, etc.
          kill_GX2();
          takedown_scene();
          free_MEM1_buffers();
          initialized = false;
          
          ProcUIDrawDoneRelease();
      }
      else if(status == PROCUI_STATUS_IN_FOREGROUND)
      {
         // Executed while app is in foreground
         if(!initialized)
         {
            memoryInitialize();
         
            //! allocate MEM2 command buffer memory
            gx2CommandBuffer = MEM2_alloc(0x400000, 0x40);

            //! initialize GX2 command buffer
            //TODO: wut needs defines for these transferred from Decaf
            u32 gx2_init_attributes[9];
            gx2_init_attributes[0] = 1; //CommandBufferPoolBase
            gx2_init_attributes[1] = (u32)gx2CommandBuffer;
            gx2_init_attributes[2] = 2; //CommandBufferPoolSize
            gx2_init_attributes[3] = 0x400000;
            gx2_init_attributes[4] = 7; //ArgC
            gx2_init_attributes[5] = 0;
            gx2_init_attributes[6] = 8; //ArgV
            gx2_init_attributes[7] = 0;
            gx2_init_attributes[8] = 0; //end
            GX2Init(gx2_init_attributes);
            
            //! allocate memory and setup context state TV
            tvContextState = (GX2ContextState*)MEM2_alloc(sizeof(GX2ContextState), 0x100);
            GX2SetupContextStateEx(tvContextState, true);

            //! allocate memory and setup context state DRC
            drcContextState = (GX2ContextState*)MEM2_alloc(sizeof(GX2ContextState), 0x100);
            GX2SetupContextStateEx(drcContextState, true);

            u32 scanBufferSize = 0;
            s32 scaleNeeded = 0;

            s32 tvScanMode = GX2_TV_RENDER_MODE_WIDE_1080P; //GX2GetSystemTVScanMode(); //TODO: wut needs these functions
            s32 drcScanMode = GX2_TV_RENDER_MODE_WIDE_1080P; //GX2GetSystemDRCScanMode();

            s32 tvRenderMode;
            u32 tvWidth = 0;
            u32 tvHeight = 0;

            switch(tvScanMode)
            {
            case GX2_TV_RENDER_MODE_WIDE_480P:
                tvWidth = 854;
                tvHeight = 480;
                tvRenderMode = GX2_TV_RENDER_MODE_WIDE_480P;
                break;
            case GX2_TV_RENDER_MODE_WIDE_1080P:
                tvWidth = 1920;
                tvHeight = 1080;
                tvRenderMode = GX2_TV_RENDER_MODE_WIDE_1080P;
                break;
            case GX2_TV_RENDER_MODE_WIDE_720P:
            default:
                tvWidth = 1280;
                tvHeight = 720;
                tvRenderMode = GX2_TV_RENDER_MODE_WIDE_720P;
                break;
            }

            s32 tvAAMode = GX2_AA_MODE1X;
            s32 drcAAMode = GX2_AA_MODE1X;
            
            u32 size, align;
            u32 surface_format = GX2_ATTRIB_FORMAT_UNORM_8_8_8_8 | 0x10;

            //Allocate scan buffer for TV
            GX2CalcTVSize(tvRenderMode, surface_format, GX2_BUFFERING_MODE_DOUBLE, &scanBufferSize, &scaleNeeded);
            tvScanBuffer = MEMBucket_alloc(scanBufferSize, 0x1000);
            GX2Invalidate(GX2_INVALIDATE_CPU, tvScanBuffer, scanBufferSize);
            GX2SetTVBuffer(tvScanBuffer, scanBufferSize, tvRenderMode, surface_format, GX2_BUFFERING_MODE_DOUBLE);

            //Allocate scan buffer for DRC
            GX2CalcDRCSize(drcScanMode, surface_format, GX2_BUFFERING_MODE_DOUBLE, &scanBufferSize, &scaleNeeded);
            drcScanBuffer = MEMBucket_alloc(scanBufferSize, 0x1000);
            GX2Invalidate(GX2_INVALIDATE_CPU, drcScanBuffer, scanBufferSize);
            GX2SetDRCBuffer(drcScanBuffer, scanBufferSize, drcScanMode, surface_format, GX2_BUFFERING_MODE_DOUBLE);

            //TV color buffer
            GX2InitColorBuffer(&tvColorBuffer, GX2_SURFACE_DIM_TEXTURE_2D, tvWidth, tvHeight, 1, surface_format, tvAAMode);
            tvColorBuffer.surface.image = MEM1_alloc(tvColorBuffer.surface.imageSize, tvColorBuffer.surface.alignment);
            GX2Invalidate(GX2_INVALIDATE_CPU, tvColorBuffer.surface.image, tvColorBuffer.surface.imageSize);

            //TV depth buffer
            GX2InitDepthBuffer(&tvDepthBuffer, GX2_SURFACE_DIM_TEXTURE_2D, tvColorBuffer.surface.width, tvColorBuffer.surface.height, 1, GX2_SURFACE_FORMAT_FLOAT_R32, tvAAMode);
            tvDepthBuffer.surface.image = MEM1_alloc(tvDepthBuffer.surface.imageSize, tvDepthBuffer.surface.alignment);
            GX2Invalidate(GX2_INVALIDATE_CPU, tvDepthBuffer.surface.image, tvDepthBuffer.surface.imageSize);

            //TV HiZ buffer
            GX2InitDepthBufferHiZEnable(&tvDepthBuffer, true);
            GX2CalcDepthBufferHiZInfo(&tvDepthBuffer, &size, &align);
            tvDepthBuffer.hiZPtr = MEM1_alloc(size, align);
            GX2Invalidate(GX2_INVALIDATE_CPU, tvDepthBuffer.hiZPtr, size);

            //DRC color buffer
            GX2InitColorBuffer(&drcColorBuffer, GX2_SURFACE_DIM_TEXTURE_2D, 854, 480, 1, surface_format, drcAAMode);
            drcColorBuffer.surface.image = MEM1_alloc(drcColorBuffer.surface.imageSize, drcColorBuffer.surface.alignment);
            GX2Invalidate(GX2_INVALIDATE_CPU, drcColorBuffer.surface.image, drcColorBuffer.surface.imageSize);

            //DRC depth buffer
            GX2InitDepthBuffer(&drcDepthBuffer, GX2_SURFACE_DIM_TEXTURE_2D, drcColorBuffer.surface.width, drcColorBuffer.surface.height, 1, GX2_SURFACE_FORMAT_FLOAT_R32, drcAAMode);
            drcDepthBuffer.surface.image = MEM1_alloc(drcDepthBuffer.surface.imageSize, drcDepthBuffer.surface.alignment);
            GX2Invalidate(GX2_INVALIDATE_CPU, drcDepthBuffer.surface.image, drcDepthBuffer.surface.imageSize);

            //DRC HiZ buffer
            GX2InitDepthBufferHiZEnable(&drcDepthBuffer, true);
            GX2CalcDepthBufferHiZInfo(&drcDepthBuffer, &size, &align);
            drcDepthBuffer.hiZPtr = MEM1_alloc(size, align);
            GX2Invalidate(GX2_INVALIDATE_CPU, drcDepthBuffer.hiZPtr, size);
            
            //Allocate anti aliasing buffers to MEM2
            if (tvColorBuffer.surface.aa)
            {
                u32 auxSize, auxAlign;
                GX2CalcColorBufferAuxInfo(&tvColorBuffer, &auxSize, &auxAlign);
                tvColorBuffer.aaBuffer = MEM2_alloc(auxSize, auxAlign);

                tvColorBuffer.aaSize = auxSize;
                memset(tvColorBuffer.aaBuffer, 0xCC, auxSize);
                GX2Invalidate(GX2_INVALIDATE_CPU, tvColorBuffer.aaBuffer, auxSize);
            }

            if (drcColorBuffer.surface.aa)
            {
                u32 auxSize, auxAlign;
                GX2CalcColorBufferAuxInfo(&drcColorBuffer, &auxSize, &auxAlign);
                drcColorBuffer.aaBuffer = MEM2_alloc(auxSize, auxAlign);
                drcColorBuffer.aaSize = auxSize;
                memset(drcColorBuffer.aaBuffer, 0xCC, auxSize);
                GX2Invalidate(GX2_INVALIDATE_CPU, drcColorBuffer.aaBuffer, auxSize );
            }
             
            //Set context and buffers
            GX2SetContextState(tvContextState);
            GX2SetColorBuffer(&tvColorBuffer, GX2_RENDER_TARGET_0);
            GX2SetDepthBuffer(&tvDepthBuffer);

            GX2SetContextState(drcContextState);
            GX2SetColorBuffer(&drcColorBuffer, GX2_RENDER_TARGET_0);
            GX2SetDepthBuffer(&drcDepthBuffer);
             
            //Load all of our shaders
            setup_scene();
             
            initialized = true;
            gx2_killed = false;
            mem1_freed = false;
         }
      }
   }

   return is_app_running;
}

void prepare_render(GX2ColorBuffer * currColorBuffer, GX2DepthBuffer * currDepthBuffer, GX2ContextState * currContextState)
{
    GX2ClearColor(currColorBuffer, 0.25f, 0.0f, 0.5f, 1.0f);
    GX2ClearDepthStencilEx(currDepthBuffer, currDepthBuffer->depthClear, currDepthBuffer->stencilClear, GX2_CLEAR_FLAGS_DEPTH | GX2_CLEAR_FLAGS_STENCIL);

    GX2SetContextState(currContextState);
    GX2SetViewport(0.0f, 0.0f, currColorBuffer->surface.width, currColorBuffer->surface.height, 0.0f, 1.0f);
    GX2SetScissor(0, 0, currColorBuffer->surface.width, currColorBuffer->surface.height);

    GX2SetDepthOnlyControl(true, true, GX2_COMPARE_FUNC_LESS);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, false, true);
    GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD, true, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD);

    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, false, false);
}

//This is our actual rendering code
void render()
{
    clean_tex_allocs();
    
    box_x += 10.0f;
    if(box_x > (float)TARGET_WIDTH)
        box_x = -512.0f;
    
    render_texture(&texture, 128.0f, 128.0f, 512.0f, 512.0f);
    render_texture(&texture, box_x, 512.0f, 512.0f, 512.0f);
}

int main(int argc, char **argv)
{
   ProcUIInit(&SaveCallback);
   
   chdir("fs:/vol/content/");
   FILE *texture2DFile = fopen("texture2D.gsh", "rb");
   
   if(!texture2DFile)
   {
      OSFatal("Failed to load texture2D.gsh!");
      return 0;
   }
   
   //Load texture2D.gsh
   fseek(texture2DFile, 0, SEEK_END);
   u32 shaderSize = ftell(texture2DFile);
   rewind(texture2DFile);
   
   texture2DData = malloc(shaderSize);
   fread(texture2DData, shaderSize, sizeof(u8), texture2DFile);
   
   
   uint32_t gx2_handle;
   OSDynLoad_Acquire("gx2.rpl", &gx2_handle);
   OSDynLoad_FindExport(gx2_handle, 0, "GX2Flush", &GX2Flush);
   OSDynLoad_FindExport(gx2_handle, 0, "GX2Invalidate", &GX2Invalidate);
   OSDynLoad_FindExport(gx2_handle, 0, "GX2CalcColorBufferAuxInfo", &GX2CalcColorBufferAuxInfo);

    int vpadError = -1;
    VPADStatus vpad;

    projectionMtx = mat4_ortho(0.0f, 1.0f*TARGET_WIDTH, 0.0f, 1.0f*TARGET_HEIGHT, 0.1f, 1000.0f, NULL);

    while(app_running())
    {
        if(!initialized) continue;
        
        VPADRead(0, &vpad, 1, &vpadError);

        //Render DRC
        prepare_render(&drcColorBuffer, &drcDepthBuffer, drcContextState);
        render();
        GX2CopyColorBufferToScanBuffer(&drcColorBuffer, GX2_SCAN_TARGET_DRC);

        //Render TV
        prepare_render(&tvColorBuffer, &tvDepthBuffer, tvContextState);
        render();
        GX2CopyColorBufferToScanBuffer(&tvColorBuffer, GX2_SCAN_TARGET_TV);
        GX2SwapScanBuffers();
        GX2Flush();
        GX2DrawDone();
        
        GX2SetTVEnable(true);
        GX2SetDRCEnable(true);

        GX2WaitForVsync();
    }
   
   return 0;
}
