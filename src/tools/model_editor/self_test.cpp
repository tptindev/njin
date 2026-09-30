#include "document.h"
#include "raymath.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace model_editor {
int self_test() {
  int checks=0;
  auto check=[&](bool condition,const char *message) {
    ++checks; if (!condition) throw std::runtime_error(message);
  };
  try {
    Document d=humanoid(),loaded; std::string error;
    auto original=njin::json_dump(serialize(d)); njin::json_value json;
    check(njin::json_parse(original,json),"parse project JSON");
    check(deserialize(json,loaded,error),"load project JSON");
    check(original==njin::json_dump(serialize(loaded)),"round trip preserves model/rig/pose");
    check(!can_parent(d,0,4),"reject descendant parent");
    check(can_parent(d,4,5),"allow valid reparent");
    auto cyclic=serialize(d);
    cyclic.find("bones")->items[0].set("parent",4);
    check(!deserialize(cyclic,loaded,error),"reject cyclic JSON");
    check(original==njin::json_dump(serialize(loaded)),"failed load preserves existing document");
    auto invalid=serialize(d); invalid.find("shapes")->items[0].set("bone",999);
    check(!deserialize(invalid,loaded,error),"reject bad bone reference");
    invalid=serialize(d); invalid.find("shapes")->items[0].set("kind",0.5);
    check(!deserialize(invalid,loaded,error),"reject fractional enum");
    invalid=serialize(d); invalid.find("bones")->items[0].set("length",-1);
    check(!deserialize(invalid,loaded,error),"reject negative dimensions");
    auto rest=bone_matrices(d,false); d.bones[3].pose.z=50;
    auto pose=bone_matrices(d,true);
    check(Vector3Distance(Vector3Transform({},rest[4]),Vector3Transform({},pose[4]))>0.2f,"child follows parent pose");
    check(Vector3Distance(Vector3Transform({},rest[5]),Vector3Transform({},pose[5]))<0.0001f,"other branch unaffected");
    // Binding and deleting must preserve both translation and arbitrary rotation.
    d.shapes[4].rotation={23,37,-54};
    Matrix before=shape_matrix(d.shapes[4],bone_matrices(d,false));
    bind_shape(d,4,5);
    Matrix after=shape_matrix(d.shapes[4],bone_matrices(d,false));
    for (Vector3 p : {Vector3{},Vector3{1,0,0},Vector3{0,1,0},Vector3{0,0,1}})
      check(Vector3Distance(Vector3Transform(p,before),Vector3Transform(p,after))<0.0001f,"rebind preserves rest transform");
    remove_bone(d,5);
    after=shape_matrix(d.shapes[4],bone_matrices(d,false));
    check(d.bones.size()==9 && d.shapes[4].bone==-1,"delete subtree detaches shapes");
    check(Vector3Distance(Vector3Transform({1,2,3},before),Vector3Transform({1,2,3},after))<0.0001f,"delete preserves world transform");
    check(deserialize(serialize(d),loaded,error),"remapped rig remains valid");
    Document sphere; Shape s; s.radius=1; sphere.shapes.push_back(s);
    Field f(sphere,false);
    check(std::abs(f.distance({}))<1.001f && f.distance({})<-0.999f,"sphere interior");
    check(std::abs(f.distance({2,0,0})-1)<0.0001f,"sphere exterior");
    Shape cut=s; cut.radius=0.5f; cut.operation=2; sphere.shapes.push_back(cut);
    check(Field(sphere,false).distance({})>0.49f,"subtraction opens cavity");
    sphere.shapes.back().operation=3;
    check(Field(sphere,false).distance({0.75f,0,0})>0,"intersection clips surface");
    sphere.shapes.back().operation=1; sphere.shapes.back().radius=1;
    check(Field(sphere,false).distance({1,0,0})<0,"smooth union blends surface");
    auto mesh=triangulate(f,24);
    check(!mesh.vertices.empty() && mesh.vertices.size()%3==0,"generate triangles");
    check(mesh.vertices.size()==mesh.normals.size(),"one normal per vertex");
    float signed_volume=0;
    for (size_t i=0;i<mesh.vertices.size();i+=3) {
      auto a=mesh.vertices[i],b=mesh.vertices[i+1],c=mesh.vertices[i+2];
      check(std::abs(Vector3Length(a)-1)<0.02f,"sphere surface accuracy");
      check(Vector3DotProduct(Vector3CrossProduct(Vector3Subtract(b,a),Vector3Subtract(c,a)),a)>0,"outward winding");
      signed_volume+=Vector3DotProduct(a,Vector3CrossProduct(b,c))/6;
    }
    check(std::abs(signed_volume-4*PI/3)<0.08f,"closed sphere volume");
    auto path=std::filesystem::temp_directory_path()/"njin-model-editor-test.obj";
    check(export_obj(path.string(),mesh),"write OBJ");
    std::ifstream file(path); std::string line; size_t vertices=0,faces=0,normals=0;
    while (std::getline(file,line)) { vertices+=line.starts_with("v "); normals+=line.starts_with("vn "); faces+=line.starts_with("f "); }
    file.close(); std::filesystem::remove(path);
    check(vertices==mesh.vertices.size() && normals==vertices && faces*3==vertices,"OBJ geometry counts");
    check(triangulate(Field(Document{},false),24).vertices.empty(),"empty document");
    for (int kind=1;kind<=4;++kind) {
      Document primitive; Shape shape; shape.kind=kind; primitive.shapes.push_back(shape);
      check(!triangulate(Field(primitive,false),24).vertices.empty(),"primitive produces mesh");
    }
    Document animated=humanoid(); add_demo_animation(animated);
    auto &clip=animated.clips[0];
    check(clip.keys.size()==25,"demo has rest tracks and wave keys");
    auto middle=sample_animation(animated,clip,0.25f);
    auto rotation=matrix_euler(QuaternionToMatrix(middle[6].rotation));
    check(std::abs(rotation.z-47.5f)<0.01f,"quaternion interpolation between keys");
    auto matrices=bone_matrices(animated,true,&middle);
    auto initial=bone_matrices(animated,false);
    check(Vector3Distance(Vector3Transform({0,0.5f,0},matrices[6]),Vector3Transform({0,0.5f,0},initial[6]))>0.1f,"animated mesh attachment moves");
    AnimationClip turn; turn.duration=2;
    set_key(turn,{0,0,{0,0,0},{0,170,0}});
    set_key(turn,{0,2,{2,0,0},{0,-170,0}});
    auto halfway=sample_animation(animated,turn,1);
    check(std::abs(halfway[0].translation.x-1)<0.0001f,"linear translation interpolation");
    auto facing=Vector3RotateByQuaternion({0,0,1},halfway[0].rotation);
    check(facing.z<-0.99f,"shortest quaternion path across 180 degrees");
    check(sample_animation(animated,turn,-1)[0].translation.x==0,"sample before first key clamps");
    check(sample_animation(animated,turn,3)[0].translation.x==2,"sample at end does not wrap");
    bool playing=true;
    check(std::abs(advance_animation(1.9f,0.3f,turn,playing)-0.2f)<0.0001f && playing,"loop wraps time");
    turn.loop=false; playing=true;
    check(advance_animation(1.9f,0.3f,turn,playing)==2 && !playing,"non-loop stops at final key");
    check(advance_animation(0.5f,1,turn,playing)==0.5f,"paused playhead unchanged");
    auto count=turn.keys.size(); set_key(turn,{0,2,{3,0,0},{}});
    check(turn.keys.size()==count && turn.keys.back().translation.x==3,"key replacement has no duplicate");
    check(remove_key(turn,0,2) && !remove_key(turn,0,2),"key deletion");
    animated.bones[3].offset={0.1f,0.2f,0.3f};
    auto with_clips=serialize(animated);
    check(deserialize(with_clips,loaded,error),"animation project loads");
    check(njin::json_dump(with_clips)==njin::json_dump(serialize(loaded)),"animation JSON round trip");
    auto legacy=serialize(humanoid()); legacy.set("version",1);
    check(deserialize(legacy,loaded,error) && loaded.clips.empty(),"legacy version 1 loads without animation");
    auto bad=with_clips;
    bad.find("clips")->items[0].find("keys")->items[0].set("bone",128);
    check(!deserialize(bad,loaded,error),"reject invalid animation bone");
    bad=with_clips; bad.find("clips")->items[0].find("keys")->items[0].set("time",9);
    check(!deserialize(bad,loaded,error),"reject key beyond clip duration");
    bad=with_clips; bad.find("clips")->items[0].set("duration",0);
    check(!deserialize(bad,loaded,error),"reject zero animation duration");
    bad=with_clips; bad.find("clips")->items[0].find("keys")->items.push_back(bad["clips"][0]["keys"][0]);
    check(!deserialize(bad,loaded,error),"reject duplicate key time");
    remove_bone(animated,3);
    check(deserialize(serialize(animated),loaded,error),"deleting bone remaps animation tracks");
    bool wave_survived=false;
    for (const auto &key:animated.clips[0].keys)
      if (key.time==0.5f && animated.bones[key.bone].name=="Forearm.R") wave_survived=true;
    check(wave_survived,"unrelated bone animation survives subtree deletion");
    std::printf("PASS: %d checks (JSON, rig transforms, CSG, meshing, OBJ, animation)\n",checks);
    return 0;
  } catch (const std::exception &e) { std::fprintf(stderr,"FAIL after %d checks: %s\n",checks,e.what()); return 1; }
}
}
