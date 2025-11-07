/*
 * Copyright 2025 NXP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SHADER_DEWARP_TEX_H
#define SHADER_DEWARP_TEX_H

const char pixShader_dewarpTexture[] = "#version 300 es                                                 \n"
                                       "precision mediump float;                                        \n"
                                       "uniform highp sampler2D texDewarpMap;                           \n"
                                       "uniform sampler2D tex;                                          \n"
                                       "in vec2 uv;                                                     \n"
                                       "out vec4 color;                                                 \n"
                                       "void main()                                                     \n"
                                       "{                                                               \n"
                                       "    vec2 coord = texture(texDewarpMap, uv).rg;                  \n"
                                       "    color = vec4(texture(tex, vec2(coord.g, coord.r)).rgb, 1.0);        \n"
                                       "}                                                               \n";

#endif  // SHADER_DEWARP_TEX_H
