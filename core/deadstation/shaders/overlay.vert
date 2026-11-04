#version 450
// A rectangle of the page (in clip space), the overlay's texture over it.
layout(push_constant) uniform Overlay { vec4 rect; vec2 uvScale; } overlay;
layout(location = 0) out vec2 uv;

void main()
{
	vec2 corner = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);
	uv = corner * overlay.uvScale;
	gl_Position = vec4(mix(overlay.rect.xy, overlay.rect.zw, corner), 0.0, 1.0);
}
