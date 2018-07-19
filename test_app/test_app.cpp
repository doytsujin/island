#include "test_app/test_app.h"

#include "pal_window/pal_window.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"
#include "le_renderer/private/hash_util.h"

#define VULKAN_HPP_NO_SMART_HANDLE
#include "vulkan/vulkan.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_LEFT_HANDED       // vulkan uses left handed coordinate system
#include "libs/glm/glm/glm.hpp"
#include "libs/glm/glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>

#include "horse_image.h"
#include "libs/imgui/include/imgui.h"

struct le_graphics_pipeline_state_o; // owned by renderer

struct FontTextureInfo {
	uint8_t *pixels;
	int32_t  width;
	int32_t  height;
	uint64_t le_texture_handle;
	uint64_t le_image_handle;
};

struct test_app_o {
	std::unique_ptr<le::Backend>  backend;
	std::unique_ptr<pal::Window>  window;
	std::unique_ptr<le::Renderer> renderer;
	le_graphics_pipeline_state_o *psoMain;           // owned by the renderer
	le_graphics_pipeline_state_o *psoFullScreenQuad; // owned by the renderer
	le_graphics_pipeline_state_o *psoImgui;          // owned by renderer
	ImGuiContext *                imguiContext  = nullptr;
	uint64_t                      frame_counter = 0;

	FontTextureInfo imguiTexture = {};

	// NOTE: RUNTIME-COMPILE : If you add any new things during run-time, make sure to only add at the end of the object,
	// otherwise all pointers above will be invalidated. this might also overwrite memory which
	// is stored after this object, which is very subtle in introducing errors. We need to think about a way of serializing
	// and de-serializing objects which are allocated on the heap. we don't have to worry about objects which are allocated
	// on the stack, as the stack acts like a pool allocator, and they are only alife while control visits the code section
	// in question.
};

constexpr auto IMGUI_FONT_IMAGE   = RESOURCE_IMAGE_ID( "imgui-font-atlas" );
constexpr auto IMGUI_FONT_TEXTURE = RESOURCE_TEXTURE_ID( "imgui-font-atlas" );

// ----------------------------------------------------------------------

static void initialize() {

	static_assert( const_char_hash64( "resource-image-testing" ) == RESOURCE_IMAGE_ID( "testing" ), "hashes must match" );
	static_assert( const_char_hash64( "resource-buffer-testing" ) == RESOURCE_BUFFER_ID( "testing" ), "hashes must match" );
	static_assert( RESOURCE_IMAGE_ID( "testing" ) != RESOURCE_BUFFER_ID( "testing" ), "buffer and image resources can't have same id based on same name" );

	pal::Window::init();
};

// ----------------------------------------------------------------------

static void terminate() {
	pal::Window::terminate();
};

// ----------------------------------------------------------------------

static test_app_o *test_app_create() {
	auto app = new ( test_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 640 )
	    .setHeight( 480 )
	    .setTitle( "Hello world" );

	// create a new window
	app->window = std::make_unique<pal::Window>( settings );

	le_backend_vk_settings_t backendCreateInfo;
	backendCreateInfo.requestedExtensions = pal::Window::getRequiredVkExtensions( &backendCreateInfo.numRequestedExtensions );

	app->backend = std::make_unique<le::Backend>( &backendCreateInfo );

	// We need a valid instance at this point.
	app->backend->createWindowSurface( *app->window );
	app->backend->createSwapchain( nullptr ); // TODO (swapchain) - make it possible to set swapchain parameters

	app->backend->setup();

	app->renderer = std::make_unique<le::Renderer>( *app->backend );
	app->renderer->setup();

	{
		// -- Declare graphics pipeline state objects

		// Creating shader modules will eventually compile shader source code from glsl to spir-v
		auto defaultVertShader        = app->renderer->createShaderModule( "./shaders/default.vert", LeShaderType::eVert );
		auto defaultFragShader        = app->renderer->createShaderModule( "./shaders/default.frag", LeShaderType::eFrag );
		auto fullScreenQuadVertShader = app->renderer->createShaderModule( "./shaders/fullscreenQuad.vert", LeShaderType::eVert );
		auto fullScreenQuadFragShader = app->renderer->createShaderModule( "./shaders/fullscreenQuad.frag", LeShaderType::eFrag );
		auto imguiVertShader          = app->renderer->createShaderModule( "./shaders/imgui.vert", LeShaderType::eVert );
		auto imguiFragShader          = app->renderer->createShaderModule( "./shaders/imgui.frag", LeShaderType::eFrag );

		{
			// create default pipeline
			le_graphics_pipeline_create_info_t pi;
			pi.shader_module_frag = defaultFragShader;
			pi.shader_module_vert = defaultVertShader;

			// The pipeline state object holds all state for the pipeline,
			// that's links to shader modules, blend states, input assembly, etc...
			// Everything, in short, but the renderpass, and subpass (which are added at the last minute)
			//
			// The backend pipeline object is compiled on-demand, when it is first used with a renderpass, and henceforth cached.
			auto psoHandle = app->renderer->createGraphicsPipelineStateObject( &pi );

			if ( psoHandle ) {
				app->psoMain = psoHandle;
			} else {
				std::cerr << "declaring main pipeline failed miserably.";
			}
		}

		{
			// create pso for imgui rendering

			le_graphics_pipeline_create_info_t                   pi;
			std::array<le_vertex_input_attribute_description, 3> attrs;
			std::array<le_vertex_input_binding_description, 1>   bindings;
			{
				// location 0, binding 0
				attrs[ 0 ].location       = 0;
				attrs[ 0 ].binding        = 0;
				attrs[ 0 ].binding_offset = offsetof( ImDrawVert, pos );
				attrs[ 0 ].isNormalised   = false;
				attrs[ 0 ].type           = le_vertex_input_attribute_description::eFloat;
				attrs[ 0 ].vecsize        = 2;

				// location 1, binding 0
				attrs[ 1 ].location       = 1;
				attrs[ 1 ].binding        = 0;
				attrs[ 1 ].binding_offset = offsetof( ImDrawVert, uv );
				attrs[ 1 ].isNormalised   = false;
				attrs[ 1 ].type           = le_vertex_input_attribute_description::eFloat;
				attrs[ 1 ].vecsize        = 2;

				// location 2, binding 0
				attrs[ 2 ].location       = 2;
				attrs[ 2 ].binding        = 0;
				attrs[ 2 ].binding_offset = offsetof( ImDrawVert, col );
				attrs[ 2 ].isNormalised   = true;
				attrs[ 2 ].type           = le_vertex_input_attribute_description::eChar;
				attrs[ 2 ].vecsize        = 4;
			}
			{
				// binding 0
				bindings[ 0 ].binding    = 0;
				bindings[ 0 ].input_rate = le_vertex_input_binding_description::ePerVertex;
				bindings[ 0 ].stride     = sizeof( ImDrawVert );
			}

			pi.shader_module_frag = imguiFragShader;
			pi.shader_module_vert = imguiVertShader;

			pi.vertex_input_attribute_descriptions       = attrs.data();
			pi.vertex_input_attribute_descriptions_count = attrs.size();
			pi.vertex_input_binding_descriptions         = bindings.data();
			pi.vertex_input_binding_descriptions_count   = bindings.size();

			auto psoHandle = app->renderer->createGraphicsPipelineStateObject( &pi );

			if ( psoHandle ) {
				app->psoImgui = psoHandle;
			} else {
				std::cerr << "declaring pso for imgui failed miserably.";
			}
		}

		{
			le_graphics_pipeline_create_info_t pi;
			// create full screen quad pipeline
			pi.shader_module_vert = fullScreenQuadVertShader;
			pi.shader_module_frag = fullScreenQuadFragShader;
			auto psoHandle        = app->renderer->createGraphicsPipelineStateObject( &pi );

			if ( psoHandle ) {
				app->psoFullScreenQuad = psoHandle;
			} else {
				std::cerr << "declaring test pipeline failed miserably.";
			}
		}
	}

	app->imguiContext = ImGui::CreateContext();

	// get imgui font texture handle
	{
		ImGuiIO &io = ImGui::GetIO();
		io.Fonts->GetTexDataAsRGBA32( &app->imguiTexture.pixels, &app->imguiTexture.width, &app->imguiTexture.height );
	}

	return app;
}

// ----------------------------------------------------------------------

static float get_image_plane_distance( const le::Viewport &viewport, float fovRadians ) {
	return viewport.height / ( 2.0f * tanf( fovRadians * 0.5f ) );
}

// ----------------------------------------------------------------------

static bool test_app_update( test_app_o *self ) {

	// polls events for all windows
	// this means any window may trigger callbacks for any events they have callbacks registered.
	pal::Window::pollEvents();

	if ( self->window->shouldClose() ) {
		return false;
	}

	// Grab interface for encoder so that it can be used in callbacks -
	// making it static allows it to be visible inside the callback context,
	// and it also ensures that the registry call only happens upon first retrieval.
	static auto const &le_encoder = Registry::getApi<le_renderer_api>()->le_command_buffer_encoder_i;

	le::RenderModule mainModule{};
	{

		le::RenderPass resourcePass( "resource copy", LE_RENDER_PASS_TYPE_TRANSFER );

		resourcePass.setSetupCallback( self, []( auto pRp, auto user_data_ ) -> bool {
			auto app = static_cast<test_app_o *>( user_data_ );
			auto rp  = le::RenderPassRef{pRp};

			{
				// create image for the horse image
				le_resource_info_t imgInfo;
				imgInfo.type = LeResourceType::eImage;
				{
					auto &img         = imgInfo.image;
					img.format        = VK_FORMAT_R8G8B8A8_UNORM;
					img.flags         = 0;
					img.arrayLayers   = 1;
					img.extent.depth  = 1;
					img.extent.width  = 160;
					img.extent.height = 106;
					img.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
					img.mipLevels     = 1;
					img.samples       = VK_SAMPLE_COUNT_1_BIT;
					img.imageType     = VK_IMAGE_TYPE_2D;
					img.tiling        = VK_IMAGE_TILING_LINEAR;
				}
				rp.createResource( RESOURCE_IMAGE_ID( "horse" ), imgInfo );
			}

			{
				// create image for the horse image
				le_resource_info_t imgInfo;
				imgInfo.type = LeResourceType::eImage;
				{
					auto &img         = imgInfo.image;
					img.format        = VK_FORMAT_R8G8B8A8_UNORM;
					img.flags         = 0;
					img.arrayLayers   = 1;
					img.extent.depth  = 1;
					img.extent.width  = uint32_t( app->imguiTexture.width );
					img.extent.height = uint32_t( app->imguiTexture.height );
					img.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
					img.mipLevels     = 1;
					img.samples       = VK_SAMPLE_COUNT_1_BIT;
					img.imageType     = VK_IMAGE_TYPE_2D;
					img.tiling        = VK_IMAGE_TILING_LINEAR;
				}
				rp.createResource( IMGUI_FONT_IMAGE, imgInfo );
			}

			// create resource for imgui font texture if it does not yet exist.

			return true;
		} );

		resourcePass.setExecuteCallback( self, []( auto encoder, auto user_data_ ) {
			auto app = static_cast<test_app_o *>( user_data_ );

			// Writing is always to encoder scratch buffer memory because that's the only memory that
			// is HOST visible.
			//
			// Type of resource ownership decides whether
			// a copy is added to the queue that transfers from scratch memory
			// to GPU local memory.

			le_encoder.write_to_image( encoder, RESOURCE_IMAGE_ID( "horse" ), {160, 106}, MagickImage, sizeof( MagickImage ) );

			if ( app->imguiTexture.le_image_handle == 0 ) {
				// tell encoder to upload imgui image - but only once
				// note that we use the le_image_handle field to signal that the image has been uploaded.
				size_t              numBytes = size_t( app->imguiTexture.width ) * size_t( app->imguiTexture.height ) * 32;
				LeBufferWriteRegion region   = {uint32_t( app->imguiTexture.width ), uint32_t( app->imguiTexture.height )};
				le_encoder.write_to_image( encoder, IMGUI_FONT_IMAGE, region, app->imguiTexture.pixels, numBytes );
				app->imguiTexture.le_image_handle   = IMGUI_FONT_IMAGE;
				app->imguiTexture.le_texture_handle = IMGUI_FONT_TEXTURE;
			}
		} );

		le::RenderPass renderPassFinal( "root", LE_RENDER_PASS_TYPE_DRAW );

		renderPassFinal.setSetupCallback( self, []( auto pRp, auto user_data_ ) -> bool {
			auto rp  = le::RenderPassRef{pRp};
			auto app = static_cast<test_app_o *>( user_data_ );

			// why do we let imageAttachmentInfo specify format?
			// because we might want to use a different format than the format the image is originally in.
			// this is important for example, when using a depth buffer for shadow sampling later.

			LeImageAttachmentInfo colorAttachmentInfo{};
			colorAttachmentInfo.format       = vk::Format::eB8G8R8A8Unorm; // TODO (swapchain): use swapchain image format programmatically
			colorAttachmentInfo.access_flags = le::AccessFlagBits::eWrite;
			colorAttachmentInfo.loadOp       = LE_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachmentInfo.storeOp      = LE_ATTACHMENT_STORE_OP_STORE;
			rp.addImageAttachment( RESOURCE_IMAGE_ID( "backbuffer" ), &colorAttachmentInfo );

			rp.useResource( RESOURCE_IMAGE_ID( "horse" ), le::AccessFlagBits::eRead );

			// this will create an imageView and a sampler in the context of this pass/encoder.
			// this will implicitly use the resource for reading

			{
				LeTextureInfo textureInfo;
				textureInfo.imageView.imageId = RESOURCE_IMAGE_ID( "horse" );
				textureInfo.imageView.format  = VK_FORMAT_R8G8B8A8_UNORM;
				textureInfo.sampler.magFilter = VK_FILTER_NEAREST;
				textureInfo.sampler.minFilter = VK_FILTER_LINEAR;

				rp.sampleTexture( RESOURCE_TEXTURE_ID( "texture1" ), textureInfo );
			}
			{
				// register that we want to use the imgui texture in this renderpass
				LeTextureInfo textureInfo;
				textureInfo.imageView.imageId = IMGUI_FONT_IMAGE;
				textureInfo.imageView.format  = VK_FORMAT_R8G8B8A8_UNORM;
				textureInfo.sampler.magFilter = VK_FILTER_NEAREST;
				textureInfo.sampler.minFilter = VK_FILTER_NEAREST;

				rp.sampleTexture( IMGUI_FONT_TEXTURE, textureInfo );
			}

			rp.setIsRoot( true );

			return true;
		} );

		renderPassFinal.setExecuteCallback( self, []( le_command_buffer_encoder_o *encoder, void *user_data ) {
			static auto const &le_encoder = Registry::getApi<le_renderer_api>()->le_command_buffer_encoder_i;

			auto app = static_cast<test_app_o *>( user_data );

			auto screenWidth  = app->window->getSurfaceWidth();
			auto screenHeight = app->window->getSurfaceHeight();

			le::Viewport viewports[ 2 ] = {
			    {0.f, 0.f, float( screenWidth ), float( screenHeight ), 0.f, 1.f},
			    {10.f, 10.f, 160.f * 3.f + 10.f, 106.f * 3.f + 10.f, 0.f, 1.f},
			};

			le::Rect2D scissors[ 2 ] = {
			    {0, 0, screenWidth, screenHeight},
			    {10, 10, 160 * 3 + 10, 106 * 3 + 10},
			};

			glm::vec3 fullScreenQuadData[ 3 ] = {
			    {0, 0, 0},
			    {2, 0, 0},
			    {0, 2, 0},
			};

			glm::vec3 triangleData[ 3 ] = {
			    {-50, -50, 0},
			    {50, -50, 0},
			    {0, 50, 0},
			};

			uint16_t indexData[ 3 ] = {0, 1, 2};

			// data as it is laid out in the ubo for the shader
			struct ColorUbo_t {
				glm::vec4 color;
			};

			struct MatrixStackUbo_t {
				glm::mat4 modelMatrix;
				glm::mat4 viewMatrix;
				glm::mat4 projectionMatrix;
			};

			float r_val = ( app->frame_counter % 120 ) / 120.f;

			ColorUbo_t ubo1{{1, 0, 0, 1}};

			// Bind full screen quad pipeline
			{
				le_encoder.set_vertex_data( encoder, triangleData, sizeof( glm::vec3 ) * 3, 0 );

				le_encoder.bind_graphics_pipeline( encoder, app->psoFullScreenQuad );
				le_encoder.set_argument_texture( encoder, RESOURCE_TEXTURE_ID( "texture1" ), const_char_hash64( "src_tex_unit_0" ), 0 );
				le_encoder.set_scissor( encoder, 0, 1, &scissors[ 1 ] );
				le_encoder.set_viewport( encoder, 0, 1, &viewports[ 1 ] );
				le_encoder.draw( encoder, 3, 1, 0, 0 );
			}

			// Bind full main graphics pipeline
			{
				le_encoder.bind_graphics_pipeline( encoder, app->psoMain );

				le_encoder.set_scissor( encoder, 0, 1, scissors );
				le_encoder.set_viewport( encoder, 0, 1, viewports );

				MatrixStackUbo_t matrixStack;
				matrixStack.projectionMatrix = glm::perspective( glm::radians( 60.f ), float( screenWidth ) / float( screenHeight ), 0.01f, 1000.f );
				matrixStack.modelMatrix      = glm::mat4( 1.f ); // identity matrix
				matrixStack.modelMatrix      = glm::scale( matrixStack.modelMatrix, glm::vec3( 1 ) );
				matrixStack.modelMatrix      = glm::translate( matrixStack.modelMatrix, glm::vec3( 100, 0, 0 ) );

				matrixStack.modelMatrix = glm::rotate( matrixStack.modelMatrix, glm::radians( r_val * 360 ), glm::vec3( 0, 0, 1 ) );

				float normDistance     = get_image_plane_distance( viewports[ 0 ], glm::radians( 60.f ) ); // calculate unit distance
				matrixStack.viewMatrix = glm::lookAt( glm::vec3( 0, 0, normDistance ), glm::vec3( 0 ), glm::vec3( 0, -1, 0 ) );

				le_encoder.set_argument_ubo_data( encoder, const_char_hash64( "MatrixStack" ), &matrixStack, sizeof( MatrixStackUbo_t ) ); // set a descriptor to set, binding, array_index
				le_encoder.set_argument_ubo_data( encoder, const_char_hash64( "Color" ), &ubo1, sizeof( ColorUbo_t ) );                    // set a descriptor to set, binding, array_index

				le_encoder.set_vertex_data( encoder, triangleData, sizeof( glm::vec3 ) * 3, 0 );
				le_encoder.set_index_data( encoder, indexData, sizeof( indexData ), 0 ); // 0 for indexType means uint16_t
				le_encoder.draw_indexed( encoder, 3, 1, 0, 0, 0 );
			}

			//			{
			//				// draw imgui
			//				glm::mat4 ortho_projection =
			//				{
			//					{ 2.0f / io.DisplaySize.x, 0.0f,                    0.0f, 0.0f },
			//					{ 0.0f,                    2.0f / io.DisplaySize.y, 0.0f, 0.0f },
			//					{ 0.0f,                    0.0f,                    1.0f, 0.0f },
			//					{ -1.f,                    -1.f,                    0.0f, 1.0f },
			//				};

			//				le_encoder.bind_graphics_pipeline(encoder,self->psoImgui);
			//				le_encoder.set_argument_ubo_data( encoder, const_char_hash64( "mvp" ), &ortho_projection, sizeof( glm::mat4 ) );

			//			}
		} );

		mainModule.addRenderPass( resourcePass );
		mainModule.addRenderPass( renderPassFinal );
	}

	// Update will call all rendercallbacks in this module.
	// the RECORD phase is guaranteed to execute - all rendercallbacks will get called.
	self->renderer->update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void test_app_destroy( test_app_o *self ) {
	if ( self->imguiContext ) {
		ImGui::DestroyContext( self->imguiContext );
		self->imguiContext = nullptr;
	}
	delete ( self );
}

// ----------------------------------------------------------------------

void register_test_app_api( void *api_ ) {
	auto  test_app_api_i = static_cast<test_app_api *>( api_ );
	auto &test_app_i     = test_app_api_i->test_app_i;

	test_app_i.initialize = initialize;
	test_app_i.terminate  = terminate;

	test_app_i.create  = test_app_create;
	test_app_i.destroy = test_app_destroy;
	test_app_i.update  = test_app_update;

#ifndef PLUGIN_TEST_APP_STATIC
	Registry::loadLibraryPersistently( "libimgui.so" );
#endif
}
