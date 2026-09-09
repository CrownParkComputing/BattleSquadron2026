#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
#define SAMPLE texture
#define OUTPUT finalColor
uniform sampler2D texture0; // 24x32 original map tile identities
uniform sampler2D atlas;
uniform sampler2D originalAtlas;

float tileAt(vec2 world) {
    vec2 cell = clamp(floor(world / 16.0), vec2(0.0), vec2(23.0,31.0));
    return floor(SAMPLE(texture0,(cell+0.5)/vec2(24.0,32.0)).r*255.0+0.5)-1.0;
}
vec2 atlasUV(vec2 world, float tile) {
    vec2 cell = vec2(mod(tile,5.0),floor(tile/5.0));
    // Stay inside each atlas cell to avoid bleeding from unrelated neighbours.
    return (cell + clamp(fract(world/16.0),vec2(0.004),vec2(0.996)))/5.0;
}
vec3 art(vec2 world) {
    world=clamp(world,vec2(0.001),vec2(383.999,511.999));
    float tile=tileAt(world);
    if(tile<0.0) return vec3(0.0);
    return SAMPLE(atlas,atlasUV(world,tile)).rgb;
}
void main() {
    vec2 world=fragTexCoord*vec2(384.0,512.0);
    if(any(lessThan(world,vec2(0.0))) || any(greaterThanEqual(world,vec2(384.0,512.0)))) discard;
    float tile=tileAt(world);
    if(tile<0.0) discard;
    vec3 colour=art(world);
    // Blend across the narrow joins, in world space, never moving map cells.
    vec2 edge=mod(world+8.0,16.0)-8.0;
    if(abs(edge.x)<1.0) {
        float boundary=world.x-edge.x;
        colour=mix(art(vec2(boundary-1.0,world.y)),art(vec2(boundary+1.0,world.y)),(edge.x+1.0)/2.0);
    }
    if(abs(edge.y)<1.0) {
        float boundary=world.y-edge.y;
        vec3 across=mix(art(vec2(world.x,boundary-1.0)),art(vec2(world.x,boundary+1.0)),(edge.y+1.0)/2.0);
        colour=mix(colour,across,1.0-abs(edge.y));
    }
    // The original 16x16 silhouette wins over any AI drift at a cloud edge.
    vec2 pixelWorld=floor(world)+0.5;
    vec3 reference=SAMPLE(originalAtlas,atlasUV(pixelWorld,tile)).rgb;
    if(tile>=6.0) {
        if(max(reference.r,max(reference.g,reference.b))<0.01) colour=vec3(0.0);
        else if(max(colour.r,max(colour.g,colour.b))<0.03) colour=reference;
    }
    OUTPUT=vec4(colour*0.65,1.0)*fragColor;
}
