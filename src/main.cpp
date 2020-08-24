#include <OpenGP/GL/Application.h>
#include <OpenGP/external/LodePNG/lodepng.cpp>

using namespace OpenGP;

typedef Eigen::Transform<float, 3, Eigen::Affine> Transform;

// Animation Properties
const int SPEED_FACTOR = 650;	// Speed of fox along line
float shrinkFactor = 1.2f;	// Adjusts how quickly fox gets smaller
float initialSize = 0.25f;	// Adjusts starting size of fox (larger value means smaller fox)
float sceneTime = 0.0f;		// Clock to synchronise animation
float frameTime = 0.0f;		// Clock for determining the fox frame
float scaleTime = 0.0f;		// Clock for determining scale factor of fox
float pointSize = 10.0f;	// Adjusts size of control point on screen
float width = 720.0f;		// Window width
float height = 720.0f;		// Window height
bool isEndLine = false;         // Check if fox reaches end of curve

// Include shaders
const char* fb_vshader =
#include "fb_vshader.glsl"
;
const char* fb_fshader =
#include "fb_fshader.glsl"
;
const char* quad_vshader =
#include "quad_vshader.glsl"
;
const char* quad_fshader =
#include "quad_fshader.glsl"
;
const char* line_vshader =
#include "line_vshader.glsl"
;
const char* line_fshader =
#include "line_fshader.glsl"
;

// Arrays to store control points for creating Bezier curve
std::vector<Vec2> controlPoints;
std::vector<Vec2> bezierPoints;

// Initialise shader, mesh, and texture variables
std::unique_ptr<GPUMesh> quad;		// Meshes
std::unique_ptr<GPUMesh> line;
std::unique_ptr<GPUMesh> bezierCurve;
std::unique_ptr<Shader> quadShader;	// Shaders
std::unique_ptr<Shader> fbShader;
std::unique_ptr<Shader> lineShader;
std::unique_ptr<Framebuffer> fb;    	// Framebuffer and colour texture
std::unique_ptr<RGBA8Texture> c_buf;

// Animation Frames and Background Textures
std::unique_ptr<RGBA8Texture> scene;	// Background image
std::unique_ptr<RGBA8Texture> f1;
std::unique_ptr<RGBA8Texture> f2;
std::unique_ptr<RGBA8Texture> f3;
std::unique_ptr<RGBA8Texture> f4;
std::unique_ptr<RGBA8Texture> f5;
std::unique_ptr<RGBA8Texture> f6;
std::unique_ptr<RGBA8Texture> f7;
std::unique_ptr<RGBA8Texture> f8;
std::unique_ptr<RGBA8Texture> f9;
std::unique_ptr<RGBA8Texture> f10;
std::unique_ptr<RGBA8Texture> f11;
std::unique_ptr<RGBA8Texture> f12;
std::unique_ptr<RGBA8Texture> f13;

// Used to adjust position, frame, and scale during animation
void clock() {
    sceneTime += 0.00005f;
    scaleTime += 0.0002f;
    frameTime += 0.00025f;
}

// Function for binding/unbinding textures to scene, avoids repeated code
void bindTexture(std::unique_ptr<RGBA8Texture> &tex) {
    tex->bind();
    quadShader->set_uniform("tex", 0);
    quad->set_attributes(*quadShader);
    quad->draw();
    tex->unbind();
}

// Function for building and loading textures to the animation
void loadTexture(std::unique_ptr<RGBA8Texture>& texture, const char* filename) {
    std::vector<unsigned char> image; 	// Vector to stores the actual pixels (in 1D line)
    unsigned int width, height; 	// Declare variables for texture width, height

    // Decode and write errors to standard output
    unsigned error = lodepng::decode(image, width, height, filename);
    if (error)
        std::cout << "decoder error " << error << ": " << lodepng_error_text(error) << std::endl;

    // Get each row of pixels in the texture (row-by-row) by accessing the image vector declared above
    // We use 4*width as there are four chars per pixel (Red, Green, Blue, Alpha)
    unsigned char* row = new unsigned char[4 * width];

    // Write out rows to draw texture, and flip
    for (int i = 0; i < int(height) / 2; i++) {
        memcpy(row, &image[4 * i * width], 4 * width * sizeof(unsigned char));
        memcpy(&image[4 * i * width],
               &image[image.size() - 4 * (i + 1) * width],
               4 * width * sizeof(unsigned char));
        memcpy(&image[image.size() - 4 * (i + 1) * width], row, 4 * width * sizeof(unsigned char));
    }

    delete row;

    // Push texture to animation
    texture = std::unique_ptr<RGBA8Texture>(new RGBA8Texture());
    texture->upload_raw(width, height, &image[0]);
}

void buildCurve() {
    // Vector to store discrete segments of curve
    std::vector<unsigned int> curve;
    // Vector to store appropriate Bezier points (values) along the curve
    bezierPoints = std::vector<Vec2>();

    // Formula for computing Bezier curve. Loop for sum
    for (int i = 0; i < 150 + 1; i++) {
        float t = i / 150.0f;
        // Calculate value for Bezier point using general formula (1-t)^(n-1)...
        Vec2 b = std::pow((1-t),3) * controlPoints[0]
                       + std::pow((1-t),2) * controlPoints[1] * 3 * t
                       + (1-t) * controlPoints[2] * std::pow(t,2)
                       + std::pow(t,3) * controlPoints[3];

        // Save Bezier point in array, get the next
        bezierPoints.push_back(b);
        // Save curve segment, and get the next
        curve.push_back(i);
    }

    // Set curve
    bezierCurve->set_vbo<Vec2>("vposition", bezierPoints);
    bezierCurve->set_triangles(curve);
}

// Initialises the scene's quad, sets vertex coordinates
void quadInitialisation() {
    quad = std::unique_ptr<GPUMesh>(new GPUMesh());

    // Set values to correspond to quad's four corners
    std::vector<Vec3> quad_vposition = {
        Vec3(-1, -1, 0), Vec3(-1, 1, 0),
        Vec3(1, -1, 0), Vec3(1, 1, 0)
    };
    quad->set_vbo<Vec3>("vposition", quad_vposition);
    std::vector<unsigned int> quad_triangle_indices = { 0, 2, 1, 1, 2, 3 };

    // Store and assign the quad's vertex coordinates (from triangles)
    quad->set_triangles(quad_triangle_indices);
    std::vector<Vec2> quad_vtexcoord = {
        Vec2(0, 0),
        Vec2(0, 1),
        Vec2(1, 0),
        Vec2(1, 1)
    };
    quad->set_vtexcoord(quad_vtexcoord);
}

// Initialises the animation by setting up framebuffer, shaders, textures, and the Bezier curve
void initialise() {
    // Set background colour (white)
    glClearColor(1, 1, 1, 1.0f);

    // Initialise framebuffer and colour buffer texture
    c_buf = std::unique_ptr<RGBA8Texture>(new RGBA8Texture());
    // Allocate memory
    c_buf->allocate(width * 2, height * 2);
    fb = std::unique_ptr<Framebuffer>(new Framebuffer());
    fb->attach_color_texture(*c_buf);

    // Initialise framebuffer shader
    fbShader = std::unique_ptr<Shader>(new Shader());
    fbShader->verbose = true;
    fbShader->add_vshader_from_source(fb_vshader);
    fbShader->add_fshader_from_source(fb_fshader);
    fbShader->link();

    // Initialise line shader
    lineShader = std::unique_ptr<Shader>(new Shader());
    lineShader->verbose = true;
    lineShader->add_vshader_from_source(line_vshader);
    lineShader->add_fshader_from_source(line_fshader);
    lineShader->link();

    // Initialise quad shader
    quadShader = std::unique_ptr<Shader>(new Shader());
    quadShader->verbose = true;
    quadShader->add_vshader_from_source(quad_vshader);
    quadShader->add_fshader_from_source(quad_fshader);
    quadShader->link();

    // Initialise scene's quad by calling the above function
    quadInit();

    // Create vector to store (initial) hard-coded Bezier control points
    controlPoints = std::vector<Vec2>();
    controlPoints.push_back(Vec2(-0.8f, -1.6f));	// Lower-left hand corner
    controlPoints.push_back(Vec2(-0.45f, 0.7f));	// Upper-middle
    controlPoints.push_back(Vec2(-0.05f, 0.0f));	// Lower-middle
    controlPoints.push_back(Vec2(1.8f, 0.1f));		// Right-side
    bezierCurve = std::unique_ptr<GPUMesh>(new GPUMesh());
    // Call function to create the Bezier curve with above parameters
    buildCurve();

    // Initialise line to connect control points
    line = std::unique_ptr<GPUMesh>(new GPUMesh());
    line->set_vbo<Vec2>("vposition", controlPoints);
    // Set points in order initialised above
    std::vector<unsigned int> lineIndices = {0, 1, 2, 3};
    line->set_triangles(lineIndices);

    // Initialise all textures for the animation (f1-f13 are individual frames)
    loadTexture(scene, "Images/background.png");
    loadTexture(f1, "Images/1.png");
    loadTexture(f2, "Images/2.png");
    loadTexture(f3, "Images/3.png");
    loadTexture(f4, "Images/4.png");
    loadTexture(f5, "Images/5.png");
    loadTexture(f6, "Images/6.png");
    loadTexture(f7, "Images/7.png");
    loadTexture(f8, "Images/8.png");
    loadTexture(f9, "Images/9.png");
    loadTexture(f10, "Images/10.png");
    loadTexture(f11, "Images/11.png");
    loadTexture(f12, "Images/12.png");
    loadTexture(f13, "Images/13.png");
}

void drawScene() {
    // Alpha blending to make frame backgrounds transparent
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw background
    quadShader->bind();
    quadShader->set_uniform("M", Transform::Identity().matrix());
    glActiveTexture(GL_TEXTURE0);
    scene->bind();
    quadShader->set_uniform("tex", 0);
    quad->set_attributes(*quadShader);
    quad->draw();
    scene->unbind();

    // Creating transformation for fox
    // Fox runs into distance, gets smaller, runs back around and grows in size
    Transform fox = Transform::Identity();

    // Variable to store fox's position on curve
    int curvePosition = (int)(sceneTime * SPEED_FACTOR)%150;
    // If fox gets to end of curve, reset scale clock
    if (curvePosition == 149) scaleTime = 0.0f;
    // Determine scale of fox
    float scaleFactor = 1/(shrinkFactor*(scaleTime+initialSize));

    // Create translation along Bezier curve and scaling based on above paramteters
    fox *= Eigen::Translation3f(bezierPoints[curvePosition].x(),
                                bezierPoints[curvePosition].y(), 0.0f);
    fox *= Eigen::AlignedScaling3f(scaleFactor*0.195f, scaleFactor*0.15f, 1.0f);

    quadShader->bind();
    quadShader->set_uniform("M", fox.matrix());
    glActiveTexture(GL_TEXTURE0);

    // Determine which frame of animation to use based on frameTime clock, and draw to scene
    unsigned int frameSelect = (int)(frameTime*100)%13;
    switch(frameSelect) {
        case 0:
            bindTexture(f1);
            break;
        case 1:
            bindTexture(f2);
            break;
        case 2:
            bindTexture(f3);
            break;
        case 3:
            bindTexture(f4);
            break;
        case 4:
            bindTexture(f5);
            break;
        case 5:
            bindTexture(f6);
            break;
        case 6:
            bindTexture(f7);
            break;
        case 7:
            bindTexture(f8);
            break;
        case 8:
            bindTexture(f9);
            break;
        case 9:
            bindTexture(f10);
            break;
        case 10:
            bindTexture(f11);
            break;
        case 11:
            bindTexture(f12);
            break;
        case 12:
            bindTexture(f13);
            break;
    }

    // Unbind quad
    quadShader->unbind();
    glDisable(GL_BLEND);
}

int main() {
    Application app;
    initialise();

    // Mouse position and selected point
    Vec2 position {0, 0};
    Vec2* selection = nullptr;

    // Create window to display animation
    Window& window = app.create_window([&](Window& window) {
        glViewport(0, 0, 1440, 1440);
        glClear(GL_COLOR_BUFFER_BIT);
        glPointSize(pointSize);

        // Rendering framebuffer and drawing scene w/ animation
        fb->bind();
        glClear(GL_COLOR_BUFFER_BIT);
        drawScene();
        fb->unbind();

        // Render to window
        fbShader->bind();
        glClear(GL_COLOR_BUFFER_BIT);

        // Bind texture and set uniforms
        glActiveTexture(GL_TEXTURE0);
        c_buf->bind();
        fbShader->set_uniform("tex", 0);
        fbShader->set_uniform("tex_width", float(width));
        fbShader->set_uniform("tex_height", float(height));
        quad->set_attributes(*fbShader);
        quad->draw();

        c_buf->unbind();
        fbShader->unbind();

        /// COMMENT TO NOT DRAW BEZIER LINE + POINTS ***************************************************
        // Rendering the straight lines and Bezier curve (overtop of animation)
        // Bind line shader
        lineShader->bind();

        // Draw Bezier curve
        lineShader->set_uniform("selection", -1);
        bezierCurve->set_attributes(*lineShader);
        bezierCurve->set_mode(GL_LINE_STRIP);
        bezierCurve->draw();

        // Setting line shader, and drawing line (colour adjusted in lineShader)
        lineShader->set_uniform("selection", -1);
        line->set_attributes(*lineShader);
        line->set_mode(GL_LINE_STRIP);
        line->draw();

        // Drawing individual points, change colour when selected (change in shader)
        if(selection!=nullptr) lineShader->set_uniform("selection", int(selection-&controlPoints[0]));
        line->set_mode(GL_POINTS);
        line->draw();

        lineShader->unbind();
        ///********************************************************************************************
    });
    // Set window attributes
    window.set_title("Fox Runs in Desert");
    window.set_size(width, height);

    // Functions for detecting and responding to input from mouse
    // Mouse movement listener
    window.add_listener<MouseMoveEvent>([&](const MouseMoveEvent& m) {
        // Mouse position in clip coordinates
        Vec2 centre = {m.position.x() / width, -m.position.y() / height};
        Vec2 p = 2.0f * (centre - Vec2(0.5f, -0.5f));
        if (selection && (p - position).norm() > 0.0f) {
            // Change positions if there was been input
            selection->x() = position.x();
            selection->y() = position.y();
            // Rebuild curve
            buildCurve();
            line->set_vbo<Vec2>("vposition", controlPoints);
        }
        position = p;
    });

    // Mouse click listener
    window.add_listener<MouseButtonEvent>([&](const MouseButtonEvent& e) {
        // If mouse is clicked...
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && !e.released) {
            selection = nullptr;
            for (auto&& v : controlPoints) {
                if ((v - position).norm() < pointSize / std::min(width,height)) {
                    selection = &v;
                    break;
                }
            }
        }
        // If mouse is released....
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.released) {
            if (selection) {
                selection->x() = position.x();
                selection->y() = position.y();
                selection = nullptr;
                line->set_vbo<Vec2>("vposition", controlPoints);
            }
        }
    });

    return app.run();
    return 0;
}
