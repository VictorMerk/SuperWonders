#version 450
layout(location=0) out vec4 colour0;
layout(location=1) out vec4 colour1;
void main() {
    colour0 = vec4(1.0,0.0,0.0,0.5);
    colour1 = vec4(0.0,1.0,0.0,0.25);
}
