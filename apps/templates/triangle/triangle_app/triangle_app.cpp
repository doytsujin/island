#include "triangle_app.h"

#include "le_window.h"
#include "le_renderer.hpp"
#include "le_pipeline_builder.h"
#include "le_camera.h"
#include "le_ui_event.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

struct triangle_app_o {
	le::Window   window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	LeCamera           camera;
	LeCameraController cameraController;
};

typedef triangle_app_o app_o;

// ----------------------------------------------------------------------

static void app_initialize() {
	le::Window::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

static void app_reset_camera( app_o* self ); // ffdecl.

// ----------------------------------------------------------------------

static app_o* app_create() {
	auto app = new ( app_o );

	le::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 1024 )
	    .setTitle( "Island // TriangleApp" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( le::RendererInfoBuilder( app->window ).build() );

	// Set up the camera
	app_reset_camera( app );

	return app;
}

// ----------------------------------------------------------------------

static void app_reset_camera( app_o* self ) {
	le::Extent2D extents{};
	self->renderer.getSwapchainExtent( &extents.width, &extents.height );
	self->camera.setViewport( { 0, 0, float( extents.width ), float( extents.height ), 0.f, 1.f } );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 view_matrix = glm::lookAt( glm::vec3{ 0, 0, self->camera.getUnitDistance() }, glm::vec3{ 0 }, glm::vec3{ 0, 1, 0 } );
	self->camera.setViewMatrix( ( float* )( &view_matrix ) );
}

// ----------------------------------------------------------------------

typedef bool ( *renderpass_setup )( le_renderpass_o* pRp, void* user_data );

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o* pRp, void* user_data ) {
	auto rp  = le::RenderPass{ pRp };
	auto app = static_cast<app_o*>( user_data );

	// Attachment may be further specialised using le::ImageAttachmentInfoBuilder().
	static le_img_resource_handle LE_SWAPCHAIN_IMAGE_HANDLE = app->renderer.getSwapchainResource();

	rp
	    .addColorAttachment( LE_SWAPCHAIN_IMAGE_HANDLE, le::ImageAttachmentInfoBuilder().build() ) // color attachment
	    ;

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {

	// Draw main scene

	auto        app = static_cast<app_o*>( user_data );
	le::Encoder encoder{ encoder_ };

	auto extents = encoder.getRenderpassExtent();

	le::Viewport viewports[ 1 ] = {
	    { 0.f, 0.f, float( extents.width ), float( extents.height ), 0.f, 1.f },
	};

	app->camera.setViewport( viewports[ 0 ] );

	// Data as it is laid out in the shader ubo.
	// Be careful to respect std430 or std140 layout
	// depending on what you specify in the
	// shader.
	struct MvpUbo {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	// Create shader modules
	static auto shaderVert =
	    LeShaderModuleBuilder( encoder.getPipelineManager() )
	        .setShaderStage( le::ShaderStage::eVertex )
	        .setSourceFilePath( "./local_resources/shaders/glsl/default.vert" )
	        .setSourceLanguage( le::ShaderSourceLanguage::eGlsl )
	        .build();

	static auto shaderFrag =
	    LeShaderModuleBuilder( encoder.getPipelineManager() )
	        .setShaderStage( le::ShaderStage::eFragment )
	        .setSourceFilePath( "./local_resources/shaders/glsl/default.frag" )
	        .setSourceLanguage( le::ShaderSourceLanguage::eGlsl )
	        .build();

	// Create a pipeline using these shader modules
	static auto pipelineDefault =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage( shaderVert )
	        .addShaderStage( shaderFrag )
	        .build();

	MvpUbo mvp;
	mvp.model = glm::mat4( 1.f );                          // identity matrix
	mvp.model = glm::scale( mvp.model, glm::vec3( 4.5 ) ); // note scale by factor 4.5
	app->camera.getViewMatrix( ( float* )( &mvp.view ) );
	app->camera.getProjectionMatrix( ( float* )( &mvp.projection ) );

	glm::vec3 vertexPositions[] = {
	    { -50, -50, 0 },
	    { 50, -50, 0 },
	    { 0, 50, 0 },
	};

	glm::vec4 vertexColors[] = {
	    { 1, 0, 0, 1.f },
	    { 0, 1, 0, 1.f },
	    { 0, 0, 1, 1.f },
	};

	encoder
	    .bindGraphicsPipeline( pipelineDefault )
	    .setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &mvp, sizeof( MvpUbo ) )
	    .setVertexData( vertexPositions, sizeof( vertexPositions ), 0 )
	    .setVertexData( vertexColors, sizeof( vertexColors ), 1 )
	    .draw( 3 );
}

// ----------------------------------------------------------------------
static void app_process_ui_events( app_o* self ) {
	using namespace le_window;
	uint32_t         numEvents;
	LeUiEvent const* pEvents;
	window_i.get_ui_event_queue( self->window, &pEvents,&numEvents );

	std::vector<LeUiEvent> events{ pEvents, pEvents + numEvents };

	bool wantsToggle = false;

	for ( auto& event : events ) {
		switch ( event.event ) {
		case ( LeUiEvent::Type::eKey ): {
			auto& e = event.key;
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				if ( e.key == LeUiEvent::NamedKey::eF11 ) {
					wantsToggle ^= true;
				} else if ( e.key == LeUiEvent::NamedKey::eC ) {
					glm::mat4 view_matrix;
					self->camera.getViewMatrix( ( float* )( &view_matrix ) );
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( view_matrix ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eX ) {
					self->cameraController.setPivotDistance( 0 );
				} else if ( e.key == LeUiEvent::NamedKey::eZ ) {
					app_reset_camera( self );
					glm::mat4 view_matrix;
					self->camera.getViewMatrix( ( float* )( &view_matrix ) );
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( view_matrix ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				}

			} // if ButtonAction == eRelease

		} break;
		default:
			// do nothing
			break;
		}
	}

	auto swapchainExtent = self->renderer.getSwapchainExtent();

	self->cameraController.setControlRect( 0, 0, float( swapchainExtent.width ), float( swapchainExtent.height ) );
	self->cameraController.processEvents( self->camera, events.data(), events.size() );

	if ( wantsToggle ) {
		self->window.toggleFullscreen();
	}
}

// ----------------------------------------------------------------------

static bool app_update( app_o* self ) {

	// Polls events for all windows
	// Use `self->window.getUIEventQueue()` to fetch events.
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	// update interactive camera using mouse data
	app_process_ui_events( self );

	le::RenderGraph renderGraph{};
	{

		auto renderPassFinal =
		    le::RenderPass( "root", le::QueueFlagBits::eGraphics )
		        .setSetupCallback( self, pass_main_setup )
		        .setExecuteCallback( self, pass_main_exec ) //
		    ;

		renderGraph.addRenderPass( renderPassFinal );
	}

	self->renderer.update( renderGraph );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void app_destroy( app_o* self ) {

	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( triangle_app, api ) {
	auto  triangle_app_api_i = static_cast<triangle_app_api*>( api );
	auto& triangle_app_i     = triangle_app_api_i->triangle_app_i;

	triangle_app_i.initialize = app_initialize;
	triangle_app_i.terminate  = app_terminate;

	triangle_app_i.create  = app_create;
	triangle_app_i.destroy = app_destroy;
	triangle_app_i.update  = app_update;
}
