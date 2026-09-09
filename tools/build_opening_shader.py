#!/usr/bin/env python3
"""Write equivalent desktop/GLES opening-preview shaders from one source."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'assets/remaster'
body='''uniform sampler2D texture0; // 24x32 original map tile identities
uniform sampler2D originalAtlas;
uniform float landMode;
uniform float animationTime;

float tileAt(vec2 world) {
    vec2 cell = clamp(floor(world / 16.0), vec2(0.0), vec2(23.0,31.0));
    return floor(SAMPLE(texture0,(cell+0.5)/vec2(24.0,32.0)).r*255.0+0.5)-1.0;
}
vec2 atlasUV(vec2 world, float tile) {
    vec2 cell = vec2(mod(tile,5.0),floor(tile/5.0));
    // Stay inside each atlas cell to avoid bleeding from unrelated neighbours.
    return (cell + clamp(fract(world/16.0),vec2(0.004),vec2(0.996)))/5.0;
}
float hash(vec2 p) {
    return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);
}
float noise2(vec2 p) {
    vec2 i=floor(p), f=fract(p);
    f=f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),
               mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);
}
float cloudNoise(vec2 p) {
    float n=0.0, a=0.5;
    for(int i=0;i<4;i++) {
        n+=a*noise2(p); p=p*2.03+vec2(7.1,13.7); a*=0.5;
    }
    return n;
}
vec3 clouds(vec2 world) {
    vec2 p=world/48.0 + vec2(animationTime*0.055,-animationTime*0.035);
    vec2 warp=vec2(cloudNoise(p+vec2(animationTime*0.025,0)),cloudNoise(p+9.3));
    float n=cloudNoise(p+warp*1.8);
    float light=cloudNoise(p+warp*1.8+vec2(-0.10,-0.14));
    float edge=clamp((light-n)*3.0,-0.15,0.20);
    return mix(vec3(0.085,0.035,0.15),vec3(0.53,0.39,0.67),smoothstep(0.2,0.8,n))+edge;
}
vec3 stars(vec2 world) {
    vec3 colour=vec3(0.002,0.003,0.009);
    for(int layer=0;layer<3;layer++) {
        float l=float(layer);
        vec2 p=world+vec2(l*91.7,-animationTime*(3.0+l*6.0));
        float size=23.0+l*9.0;
        vec2 cell=floor(p/size), local=mod(p,size);
        vec2 centre=(vec2(hash(cell+l),hash(cell+19.7+l))*0.76+0.12)*size;
        float distance=length(local-centre);
        float star=1.0-smoothstep(0.15,0.55+l*0.15,distance);
        float twinkle=0.72+0.28*sin(animationTime*(0.8+l*0.2)+hash(cell)*6.28);
        colour+=mix(vec3(0.36,0.48,0.72),vec3(0.85,0.78,0.66),hash(cell+4.3))*star*twinkle;
    }
    return colour;
}
float olive(vec3 c) {
    return step(c.b+0.015,min(c.r,c.g))*(1.0-step(0.01,abs(c.r-c.g)));
}
vec3 lava(vec2 p) {
    float n=cloudNoise(p/7.0+vec2(animationTime*0.10,-animationTime*0.07));
    float heat=smoothstep(0.28,0.72,n);
    return mix(vec3(0.38,0.025,0.003),vec3(1.0,0.48,0.025),heat);
}
void main() {
    if (landMode > 0.5) {
        vec3 colour=SAMPLE(texture0,fragTexCoord).rgb;
        vec3 reference=SAMPLE(originalAtlas,fragTexCoord).rgb;
        vec2 world=fragTexCoord*vec2(384.0,768.0)-vec2(0,768);
        if (colour.b > colour.g || max(colour.r,max(colour.g,colour.b)) < 0.015)
            colour=reference;
        vec2 land=fragTexCoord*vec2(384,768);
        float brightness=max(reference.r,max(reference.g,reference.b));
        float crater=min(length((land-vec2(103,610))/vec2(10,9)),
                         length((land-vec2(247,578))/vec2(10,9)));
        vec2 dx=vec2(3.0/384.0,0), dy=vec2(0,3.0/768.0);
        vec3 a=SAMPLE(originalAtlas,fragTexCoord+dx).rgb;
        vec3 b=SAMPLE(originalAtlas,fragTexCoord-dx).rgb;
        vec3 c=SAMPLE(originalAtlas,fragTexCoord+dy).rgb;
        vec3 d=SAMPLE(originalAtlas,fragTexCoord-dy).rgb;
        float sides=olive(a)+olive(b)+olive(c)+olive(d);
        float mechanical=step(a.g+0.015,a.b)+step(b.g+0.015,b.b)+
                         step(c.g+0.015,c.b)+step(d.g+0.015,d.b);
        bool black=brightness<0.01;
        // Keep mechanical cut-outs black. Only cracks bounded by rock and
        // the two existing crater mouths can reveal molten material.
        if(black) colour=reference;
        float fissure=(1.0-smoothstep(0.055,0.20,brightness))*olive(reference);
        if(black && sides>=2.0 && mechanical<0.5) fissure=1.0;
        if(crater<1.0 && brightness<0.20) fissure=1.0-smoothstep(0.75,1.0,crater);
        float vein=1.0-smoothstep(0.035,0.15,abs(noise2(land/9.0)-0.5));
        if(crater>=1.0 && !black) fissure*=vein;
        colour=mix(colour,lava(land),clamp(fissure,0.0,1.0));
        float glow=exp(-crater*crater*1.8)*olive(reference)*0.15;
        colour+=vec3(1.0,0.19,0.015)*glow*(0.85+0.15*sin(animationTime*1.7));
        // Replace the original white surf as well as purple cloud pixels.
        // Feather using the original coastline, not a moving geometry mask.
        if(world.y>=-128.0 && brightness>0.01) {
            vec2 blur=vec2(5.0/384.0,5.0/768.0);
            float ground=olive(reference)+sides;
            ground+=olive(SAMPLE(originalAtlas,fragTexCoord+blur).rgb);
            ground+=olive(SAMPLE(originalAtlas,fragTexCoord-blur).rgb);
            ground+=olive(SAMPLE(originalAtlas,fragTexCoord+vec2(blur.x,-blur.y)).rgb);
            ground+=olive(SAMPLE(originalAtlas,fragTexCoord+vec2(-blur.x,blur.y)).rgb);
            ground=smoothstep(0.05,0.95,ground/9.0);
            colour=mix(clouds(world),colour,ground);
        }
        OUTPUT=vec4(colour,1.0)*fragColor;
        return;
    }
    vec2 world=fragTexCoord*vec2(384.0,512.0);
    if(any(lessThan(world,vec2(0.0))) || any(greaterThanEqual(world,vec2(384.0,768.0)))) discard;
    if(world.y>=512.0) {
        OUTPUT=vec4(stars(world),1.0)*fragColor;
        return;
    }
    float tile=tileAt(world);
    if(tile<0.0) {
        OUTPUT=vec4(world.y>192.0 ? stars(world) : clouds(world),1.0)*fragColor;
        return;
    }
    vec3 reference=SAMPLE(originalAtlas,atlasUV(floor(world)+0.5,tile)).rgb;
    bool space=world.y>192.0 && (tile<6.0 || max(reference.r,max(reference.g,reference.b))<0.01);
    vec3 colour=space ? stars(world) : clouds(world);
    OUTPUT=vec4(colour,1.0)*fragColor;
}

'''
for version in (100,330):
    head = '#version %d\n'%version
    if version==100:
        head+='precision highp float;\nvarying vec2 fragTexCoord;\nvarying vec4 fragColor;\n#define SAMPLE texture2D\n#define OUTPUT gl_FragColor\n'
    else:
        head+='in vec2 fragTexCoord;\nin vec4 fragColor;\nout vec4 finalColor;\n#define SAMPLE texture\n#define OUTPUT finalColor\n'
    (root/('opening-%d.fs'%version)).write_text((head+body).rstrip()+'\n')
