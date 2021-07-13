/* SpriteShader.cpp
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#include "SpriteShader.h"

#include "Point.h"
#include "Screen.h"
#include "Shader.h"
#include "Sprite.h"

#include <vector>
#include <sstream>

using namespace std;

namespace {
	Shader shader;
	GLint scaleI;
	GLint frameI;
	GLint frameCountI;
	GLint positionI;
	GLint transformI;
	GLint blurI;
	GLint clipI;
	GLint alphaI;
	GLint swizzlerI;
        GLint useNormalsI;
	GLint light1posI;
	GLint light2posI;
	GLint light1colorI;
	GLint light2colorI;
	GLint numLightsI;
	
	GLuint vao;
	GLuint vbo;

	const vector<vector<GLint>> SWIZZLE = {
		{GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA}, // red + yellow markings (republic)
		{GL_RED, GL_BLUE, GL_GREEN, GL_ALPHA}, // red + magenta markings
		{GL_GREEN, GL_RED, GL_BLUE, GL_ALPHA}, // green + yellow (freeholders)
		{GL_BLUE, GL_RED, GL_GREEN, GL_ALPHA}, // green + cyan
		{GL_GREEN, GL_BLUE, GL_RED, GL_ALPHA}, // blue + magenta (syndicate)
		{GL_BLUE, GL_GREEN, GL_RED, GL_ALPHA}, // blue + cyan (merchant)
		{GL_GREEN, GL_BLUE, GL_BLUE, GL_ALPHA}, // red and black (pirate)
		{GL_BLUE, GL_ZERO, GL_ZERO, GL_ALPHA},  // red only (cloaked)
		{GL_ZERO, GL_ZERO, GL_ZERO, GL_ALPHA}  // black only (outline)
	};
}

bool SpriteShader::useShaderSwizzle = false;

// Initialize the shaders.
void SpriteShader::Init(bool useShaderSwizzle)
{
	SpriteShader::useShaderSwizzle = useShaderSwizzle;
	
	static const char *vertexCode =
		"// vertex sprite shader\n"
		"uniform vec2 scale;\n"
		"uniform vec2 position;\n"
		"uniform mat2 transform;\n"
		"uniform vec2 blur;\n"
		"uniform float clip;\n"
		
		"in vec2 vert;\n"
		"out vec2 fragPos;\n"
		"out vec2 fragTexCoord;\n"
		
		"void main() {\n"
		"  vec2 blurOff = 2 * vec2(vert.x * abs(blur.x), vert.y * abs(blur.y));\n"
		"  fragPos = (transform * (vert + blurOff) + position) * scale;\n"
		"  gl_Position = vec4(fragPos, 0, 1);\n"
		"  vec2 texCoord = vert + vec2(.5, .5);\n"
		"  fragTexCoord = vec2(texCoord.x, max(clip, texCoord.y)) + blurOff;\n"
		"}\n";
	
	ostringstream fragmentCodeStream;
	fragmentCodeStream <<
		"// fragment sprite shader\n"
		"uniform sampler2DArray tex;\n"
		"uniform sampler2DArray normalMap;\n"
                "uniform float useNormals;\n"
                "uniform vec2 light1pos;\n"
                "uniform vec2 light2pos;\n"
                "uniform vec3 light1color;\n"
                "uniform vec3 light2color;\n"
		"uniform int numLights;\n"
		"uniform mat2 transform;\n"
		"uniform float frame;\n"
		"uniform float frameCount;\n"
		"uniform vec2 blur;\n";
	if(useShaderSwizzle) fragmentCodeStream <<
		"uniform int swizzler;\n";
	fragmentCodeStream <<
		"uniform float alpha;\n"
		"const int range = 5;\n"
		
		"in vec2 fragPos;\n"
		"in vec2 fragTexCoord;\n"
		
		"out vec4 finalColor;\n"
		
		"void main() {\n"
		"  float first = floor(frame);\n"
		"  float second = mod(ceil(frame), frameCount);\n"
		"  float fade = frame - first;\n"
		"  vec4 color;\n"
		"  vec4 normal;\n"
		"  if(blur.x == 0 && blur.y == 0)\n"
		"  {\n"
		"    if(fade != 0)\n"
		"    {\n"
		"      color = mix(\n"
		"        texture(tex, vec3(fragTexCoord, first)),\n"
		"        texture(tex, vec3(fragTexCoord, second)), fade);\n"
		"      normal = mix(\n"
		"        texture(normalMap, vec3(fragTexCoord, first)),\n"
		"        texture(normalMap, vec3(fragTexCoord, second)), fade);\n"
		"    }\n"
		"    else\n"
		"    {\n"
		"      color = texture(tex, vec3(fragTexCoord, first));\n"
		"      normal = texture(normalMap, vec3(fragTexCoord, first));\n"
		"    }\n"
		"  }\n"
		"  else\n"
		"  {\n"
		"    color = vec4(0., 0., 0., 0.);\n"
		"    normal = vec4(0., 0., 0., 0.);\n"
		"    const float divisor = range * (range + 2) + 1;\n"
		"    for(int i = -range; i <= range; ++i)\n"
		"    {\n"
		"      float scale = (range + 1 - abs(i)) / divisor;\n"
		"      vec2 coord = fragTexCoord + (blur * i) / range;\n"
		"      if(fade != 0)\n"
		"      {\n"
		"        color += scale * mix(\n"
		"          texture(tex, vec3(coord, first)),\n"
		"          texture(tex, vec3(coord, second)), fade);\n"
		"        normal += scale * mix(\n"
		"          texture(normalMap, vec3(coord, first)),\n"
		"          texture(normalMap, vec3(coord, second)), fade);\n"
		"      }\n"
		"      else\n"
		"      {\n"
		"        color += scale * texture(tex, vec3(coord, first));\n"
		"        normal += scale * texture(normalMap, vec3(coord, first));\n"
		"      }\n"
		"    }\n"
		"  }\n"
		"  \n"
		"  vec3 light = vec3(1., 1., 1.);\n"
		"  \n"
		"  if(numLights > 0 && useNormals > 0)\n"
		"  {\n"
		"    normal = normalize(vec4(transform*(2.*normal.xy-1.), normal.z, 0.));\n"
		"    \n"
		"    vec2 surfaceToLight = normalize(light1pos - fragPos);\n"
		"    float brightness = clamp(dot(normal.xyz, vec3(surfaceToLight, -1.)), 0, 1);\n"
		"    light = brightness*light1color;\n"
		"  }\n"
		"  \n"
		"  if(numLights > 1 && useNormals > 0)\n"
		"  {\n"
		"    vec2 surfaceToLight = normalize(light2pos - fragPos);\n"
		"    float brightness = clamp(dot(normal.xyz, vec3(surfaceToLight, -1.)), 0, 1);\n"
		"    light += brightness*light2color;\n"
		"  }\n";
	
	// Only included when hardware swizzle not supported, GL <3.3 and GLES
	if(useShaderSwizzle)
	{
		fragmentCodeStream <<
		"  switch (swizzler) {\n"
		"    case 0:\n"
		"      color = color.rgba;\n"
		"      break;\n"
		"    case 1:\n"
		"      color = color.rbga;\n"
		"      break;\n"
		"    case 2:\n"
		"      color = color.grba;\n"
		"      break;\n"
		"    case 3:\n"
		"      color = color.brga;\n"
		"      break;\n"
		"    case 4:\n"
		"      color = color.gbra;\n"
		"      break;\n"
		"    case 5:\n"
		"      color = color.bgra;\n"
		"      break;\n"
		"    case 6:\n"
		"      color = color.gbba;\n"
		"      break;\n"
		"    case 7:\n"
		"      color = vec4(color.b, 0.f, 0.f, color.a);\n"
		"      break;\n"
		"    case 8:\n"
		"      color = vec4(0.f, 0.f, 0.f, color.a);\n"
		"      break;\n"
		"  }\n";
	}
	fragmentCodeStream <<
		"  finalColor = color * alpha * vec4(light, 1.);\n"
		"}\n";
	
	static const string fragmentCodeString = fragmentCodeStream.str();
	static const char *fragmentCode = fragmentCodeString.c_str();
	
	shader = Shader(vertexCode, fragmentCode);
	scaleI = shader.Uniform("scale");
	frameI = shader.Uniform("frame");
	frameCountI = shader.Uniform("frameCount");
	positionI = shader.Uniform("position");
	transformI = shader.Uniform("transform");
	blurI = shader.Uniform("blur");
	clipI = shader.Uniform("clip");
	alphaI = shader.Uniform("alpha");
	if(useShaderSwizzle)
		swizzlerI = shader.Uniform("swizzler");
	useNormalsI = shader.Uniform("useNormals");
	light1posI = shader.Uniform("light1pos");
	light2posI = shader.Uniform("light2pos");
	light1colorI = shader.Uniform("light1color");
	light2colorI = shader.Uniform("light2color");
	numLightsI = shader.Uniform("numLights");
	
	glUseProgram(shader.Object());
	glUniform1i(shader.Uniform("tex"), 0);
	glUniform1i(shader.Uniform("normalMap"), 1);
	glUseProgram(0);
	
	// Generate the vertex data for drawing sprites.
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	
	GLfloat vertexData[] = {
		-.5f, -.5f,
		-.5f,  .5f,
		 .5f, -.5f,
		 .5f,  .5f
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertexData), vertexData, GL_STATIC_DRAW);
	
	glEnableVertexAttribArray(shader.Attrib("vert"));
	glVertexAttribPointer(shader.Attrib("vert"), 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
	
	// unbind the VBO and VAO
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}



void SpriteShader::Draw(const Sprite *sprite, const Point &position, float zoom, int swizzle, float frame)
{
	if(!sprite)
		return;
	
	Item item;
	item.texture = sprite->Texture();
	item.frame = frame;
	item.frameCount = sprite->Frames();
	// Position.
	item.position[0] = static_cast<float>(position.X());
	item.position[1] = static_cast<float>(position.Y());
	// Rotation (none) and scale.
	item.transform[0] = sprite->Width() * zoom;
	item.transform[3] = sprite->Height() * zoom;
	// Swizzle.
	item.swizzle = swizzle;
	
	Bind();
	Add(item);
	Unbind();
}



void SpriteShader::Bind()
{
	glUseProgram(shader.Object());
	glBindVertexArray(vao);
	
	GLfloat scale[2] = {2.f / Screen::Width(), -2.f / Screen::Height()};
	glUniform2fv(scaleI, 1, scale);
}

void SpriteShader::BindLights(std::vector<SpriteShader::Light> lights)
{
	if(lights.size() > 0)
	{
		glUniform2fv(light1posI, 1, lights[0].position);
		glUniform3fv(light1colorI, 1, lights[0].color);
		glUniform1i(numLightsI, 1);
	}
	if(lights.size() > 1)
	{
		glUniform2fv(light2posI, 1, lights[0].position);
		glUniform3fv(light2colorI, 1, lights[0].color);
		glUniform1i(numLightsI, 2);
	}
}


void SpriteShader::Add(const Item &item, bool withBlur)
{
        glActiveTexture(GL_TEXTURE0 + 0);
	glBindTexture(GL_TEXTURE_2D_ARRAY, item.texture);
        
        glActiveTexture(GL_TEXTURE0 + 1);
        if(item.normals > 0) {
            glBindTexture(GL_TEXTURE_2D_ARRAY, item.normals);
            glUniform1f(useNormalsI, 1.0);
        } else {
            glBindTexture(GL_TEXTURE_2D_ARRAY, item.texture); //Just to be safe, I don't know how the shader reacts to unbound textures
            glUniform1f(useNormalsI, 0.0);
        }
        glActiveTexture(GL_TEXTURE0 + 0);   //other draw calls might assume TextureUnit 0 to be active

	glUniform1f(frameI, item.frame);
	glUniform1f(frameCountI, item.frameCount);
	glUniform2fv(positionI, 1, item.position);
	glUniformMatrix2fv(transformI, 1, false, item.transform);
	// Special case: check if the blur should be applied or not.
	static const float UNBLURRED[2] = {0.f, 0.f};
	glUniform2fv(blurI, 1, withBlur ? item.blur : UNBLURRED);
	// Clipping has the opposite sense in the shader.
	glUniform1f(clipI, 1.f - item.clip);
	glUniform1f(alphaI, item.alpha);
	
	// Bounds check for the swizzle value:
	int swizzle = (static_cast<size_t>(item.swizzle) >= SWIZZLE.size() ? 0 : item.swizzle);
	// Set the color swizzle.
	if(SpriteShader::useShaderSwizzle)
		glUniform1i(swizzlerI, swizzle);
	else
		glTexParameteriv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_SWIZZLE_RGBA, SWIZZLE[swizzle].data());
	
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}



void SpriteShader::Unbind()
{
	glBindVertexArray(0);
	glUseProgram(0);

	glUniform1i(numLightsI, 0);
	
	// Reset the swizzle.
	if(SpriteShader::useShaderSwizzle)
		glUniform1i(swizzlerI, 0);
	else
		glTexParameteriv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_SWIZZLE_RGBA, SWIZZLE[0].data());
}
