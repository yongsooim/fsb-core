#version 450
// Integer area integration, matching present_image's half-up rounding exactly.
// The texture contains flat RGBA cells, or the source's unpacked 4x4 detail.
layout(set=2,binding=0) uniform sampler2D source_image;
layout(std140,set=3,binding=0) uniform Parameters {
    uvec4 sizes; // source width/height, destination width/height
    uvec4 options; // destination left/top, original point sampling, reserved
} p;
layout(location=0) out vec4 color;
uvec3 pixel(uvec2 at) { return uvec3(round(texelFetch(source_image,ivec2(at),0).rgb*255.0)); }
void main() {
    uvec2 at=uvec2(gl_FragCoord.xy)-p.options.xy;
    uvec2 source=p.sizes.xy, target=p.sizes.zw;
    if(p.options.z!=0u) {
        color=vec4(vec3(pixel(((at*2u+1u)*source)/(target*2u)))/255.0,1.0);
        return;
    }
    uvec2 lo=at*source, hi=(at+1u)*source;
    uvec2 first=lo/target, end=(hi+target-1u)/target;
    uvec3 sum=uvec3(0);
    for(uint y=first.y;y<end.y;++y) {
        uint wy=min(hi.y,(y+1u)*target.y)-max(lo.y,y*target.y);
        for(uint x=first.x;x<end.x;++x) {
            uint wx=min(hi.x,(x+1u)*target.x)-max(lo.x,x*target.x);
            sum+=pixel(uvec2(x,y))*(wx*wy);
        }
    }
    uint denominator=source.x*source.y;
    color=vec4(vec3((sum+denominator/2u)/denominator)/255.0,1.0);
}
