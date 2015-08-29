
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

///@TODO: USE FACE CULLING RAY TESTING!!!!!!!!!!!!!! SO... CHECK ALL TWEENS, CEILINGS AND FLOORS GENERATION!!!!

#include "bullet/btBulletCollisionCommon.h"
#include "bullet/btBulletDynamicsCommon.h"
#include "bullet/BulletCollision/CollisionDispatch/btCollisionWorld.h"
#include "bullet/BulletCollision/CollisionShapes/btCollisionShape.h"
#include "bullet/BulletDynamics/ConstraintSolver/btTypedConstraint.h"
#include "bullet/BulletCollision/CollisionDispatch/btGhostObject.h"
#include "bullet/BulletCollision/BroadphaseCollision/btCollisionAlgorithm.h"

#include "core/gl_util.h"
#include "core/gl_font.h"
#include "core/console.h"
#include "core/obb.h"
#include "render/render.h"
#include "engine_physics.h"
#include "engine.h"
#include "mesh.h"
#include "character_controller.h"
#include "script.h"
#include "entity.h"
#include "resource.h"
#include "world.h"

/*
 * INTERNAL BHYSICS CLASSES
 */
class bt_engine_ClosestRayResultCallback : public btCollisionWorld::ClosestRayResultCallback
{
public:
    bt_engine_ClosestRayResultCallback(engine_container_p cont, bool skip_ghost = false) : btCollisionWorld::ClosestRayResultCallback(btVector3(0.0, 0.0, 0.0), btVector3(0.0, 0.0, 0.0))
    {
        m_cont = cont;
        m_skip_ghost = skip_ghost;
    }

    virtual btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult,bool normalInWorldSpace)
    {
        room_p r0 = NULL, r1 = NULL;
        engine_container_p c1;

        r0 = (m_cont)?(m_cont->room):(NULL);
        c1 = (engine_container_p)rayResult.m_collisionObject->getUserPointer();
        r1 = (c1)?(c1->room):(NULL);

        if(c1 && ((c1 == m_cont) || (m_skip_ghost && (c1->collision_type == COLLISION_TYPE_GHOST))))
        {
            return 1.0;
        }

        if(!r0 || !r1)
        {
            return ClosestRayResultCallback::addSingleResult(rayResult, normalInWorldSpace);
        }

        if(r0 && r1)
        {
            if(Room_IsInNearRoomsList(r0, r1))
            {
                return ClosestRayResultCallback::addSingleResult(rayResult, normalInWorldSpace);
            }
            else
            {
                return 1.0;
            }
        }

        return 1.0;
    }

    bool               m_skip_ghost;
    engine_container_p m_cont;
};


class bt_engine_ClosestConvexResultCallback : public btCollisionWorld::ClosestConvexResultCallback
{
public:
    bt_engine_ClosestConvexResultCallback(engine_container_p cont, bool skip_ghost = false) : btCollisionWorld::ClosestConvexResultCallback(btVector3(0.0, 0.0, 0.0), btVector3(0.0, 0.0, 0.0))
    {
        m_cont = cont;
        m_skip_ghost = skip_ghost;
    }

    virtual btScalar addSingleResult(btCollisionWorld::LocalConvexResult& convexResult,bool normalInWorldSpace)
    {
        room_p r0 = NULL, r1 = NULL;
        engine_container_p c1;

        r0 = (m_cont)?(m_cont->room):(NULL);
        c1 = (engine_container_p)convexResult.m_hitCollisionObject->getUserPointer();
        r1 = (c1)?(c1->room):(NULL);

        if(c1 && ((c1 == m_cont) || (m_skip_ghost && (c1->collision_type == COLLISION_TYPE_GHOST))))
        {
            return 1.0;
        }

        if(!r0 || !r1)
        {
            return ClosestConvexResultCallback::addSingleResult(convexResult, normalInWorldSpace);
        }

        if(r0 && r1)
        {
            if(Room_IsInNearRoomsList(r0, r1))
            {
                return ClosestConvexResultCallback::addSingleResult(convexResult, normalInWorldSpace);
            }
            else
            {
                return 1.0;
            }
        }

        return 1.0;
    }

private:
    bool               m_skip_ghost;
    engine_container_p m_cont;
};


struct physics_object_s
{
    btRigidBody    *bt_body;
};

typedef struct physics_data_s
{
    // kinematic
    btRigidBody                       **bt_body;

    // dynamic
    btPairCachingGhostObject          **ghostObjects;           // like Bullet character controller for penetration resolving.
    btManifoldArray                    *manifoldArray;          // keep track of the contact manifolds
    uint16_t                            objects_count;          // Ragdoll joints
    uint16_t                            bt_joint_count;         // Ragdoll joints
    btTypedConstraint                 **bt_joints;              // Ragdoll joints

    struct engine_container_s          *cont;
}physics_data_t, *physics_data_p;


class CBulletDebugDrawer : public btIDebugDraw
{
public:
    CBulletDebugDrawer(){}
   ~CBulletDebugDrawer(){}

    virtual void   drawLine(const btVector3& from,const btVector3& to,const btVector3& color)
    {
        renderer.debugDrawer->DrawLine(from.m_floats, to.m_floats, color.m_floats, color.m_floats);
    }

    virtual void   drawLine(const btVector3& from,const btVector3& to, const btVector3& fromColor, const btVector3& toColor)
    {
        renderer.debugDrawer->DrawLine(from.m_floats, to.m_floats, fromColor.m_floats, toColor.m_floats);
    }

    virtual void   drawContactPoint(const btVector3& PointOnB,const btVector3& normalOnB,btScalar distance,int lifeTime,const btVector3& color)
    {

    }

    virtual void   reportErrorWarning(const char* warningString)
    {
        Con_AddLine(warningString, FONTSTYLE_CONSOLE_WARNING);
    }

    virtual void   draw3dText(const btVector3& location, const char* textString)
    {
        //glRasterPos3f(location.x(),  location.y(),  location.z());
        //BMF_DrawString(BMF_GetFont(BMF_kHelvetica10),textString);
    }

    virtual void   setDebugMode(int debugMode)
    {
        m_debugMode = debugMode;
    }

    virtual int    getDebugMode() const
    {
        return m_debugMode;
    }

private:
    int32_t m_debugMode;
};

btDefaultCollisionConfiguration         *bt_engine_collisionConfiguration = NULL;
btCollisionDispatcher                   *bt_engine_dispatcher = NULL;
btGhostPairCallback                     *bt_engine_ghostPairCallback = NULL;
btBroadphaseInterface                   *bt_engine_overlappingPairCache = NULL;
btSequentialImpulseConstraintSolver     *bt_engine_solver = NULL;
btDiscreteDynamicsWorld                 *bt_engine_dynamicsWorld = NULL;

CBulletDebugDrawer                       bt_debug_drawer;

uint32_t                                 collision_nodes_pool_size = 0;
uint32_t                                 collision_nodes_pool_used = 0;
struct collision_node_s                 *collision_nodes_pool = NULL;

struct collision_node_s *Physics_GetCollisionNode();

void Physics_RoomNearCallback(btBroadphasePair& collisionPair, btCollisionDispatcher& dispatcher, const btDispatcherInfo& dispatchInfo);
void Physics_InternalTickCallback(btDynamicsWorld *world, btScalar timeStep);

/* bullet collision model calculation */
btCollisionShape* BT_CSfromBBox(btScalar *bb_min, btScalar *bb_max);
btCollisionShape* BT_CSfromMesh(struct base_mesh_s *mesh, bool useCompression, bool buildBvh, bool is_static = true);
btCollisionShape* BT_CSfromHeightmap(struct room_sector_s *heightmap, struct sector_tween_s *tweens, int tweens_size, bool useCompression, bool buildBvh);

// Bullet Physics initialization.
void Physics_Init()
{
    collision_nodes_pool = (struct collision_node_s*)malloc(DEFAULT_COLLSION_NODE_POOL_SIZE * sizeof(struct collision_node_s));
    collision_nodes_pool_size = DEFAULT_COLLSION_NODE_POOL_SIZE;
    collision_nodes_pool_used = 0;

    ///collision configuration contains default setup for memory, collision setup. Advanced users can create their own configuration.
    bt_engine_collisionConfiguration = new btDefaultCollisionConfiguration();

    ///use the default collision dispatcher. For parallel processing you can use a diffent dispatcher (see Extras/BulletMultiThreaded)
    bt_engine_dispatcher = new btCollisionDispatcher(bt_engine_collisionConfiguration);
    bt_engine_dispatcher->setNearCallback(Physics_RoomNearCallback);

    ///btDbvtBroadphase is a good general purpose broadphase. You can also try out btAxis3Sweep.
    bt_engine_overlappingPairCache = new btDbvtBroadphase();
    bt_engine_ghostPairCallback = new btGhostPairCallback();
    bt_engine_overlappingPairCache->getOverlappingPairCache()->setInternalGhostPairCallback(bt_engine_ghostPairCallback);

    ///the default constraint solver. For parallel processing you can use a different solver (see Extras/BulletMultiThreaded)
    bt_engine_solver = new btSequentialImpulseConstraintSolver;

    bt_engine_dynamicsWorld = new btDiscreteDynamicsWorld(bt_engine_dispatcher, bt_engine_overlappingPairCache, bt_engine_solver, bt_engine_collisionConfiguration);
    bt_engine_dynamicsWorld->setInternalTickCallback(Physics_InternalTickCallback);
    bt_engine_dynamicsWorld->setGravity(btVector3(0, 0, -4500.0));

    bt_debug_drawer.setDebugMode(btIDebugDraw::DBG_DrawWireframe | btIDebugDraw::DBG_DrawConstraints);
    bt_engine_dynamicsWorld->setDebugDrawer(&bt_debug_drawer);
}


void Physics_Destroy()
{
    //delete dynamics world
    delete bt_engine_dynamicsWorld;

    //delete solver
    delete bt_engine_solver;

    //delete broadphase
    delete bt_engine_overlappingPairCache;

    //delete dispatcher
    delete bt_engine_dispatcher;

    delete bt_engine_collisionConfiguration;

    delete bt_engine_ghostPairCallback;

    free(collision_nodes_pool);
    collision_nodes_pool = NULL;
    collision_nodes_pool_size = 0;
    collision_nodes_pool_used = 0;
}


void Physics_StepSimulation(float time)
{
    bt_engine_dynamicsWorld->stepSimulation(time, 0);
    collision_nodes_pool_used = 0;
}

void Physics_DebugDrawWorld()
{
    bt_engine_dynamicsWorld->debugDrawWorld();
}

struct collision_node_s *Physics_GetCollisionNode()
{
    struct collision_node_s *ret = NULL;
    if(collision_nodes_pool_used < collision_nodes_pool_size)
    {
        ret = collision_nodes_pool + collision_nodes_pool_used;
        collision_nodes_pool_used++;
    }
    return ret;
}


void Physics_CleanUpObjects()
{
    if(bt_engine_dynamicsWorld != NULL)
    {
        int num_obj = bt_engine_dynamicsWorld->getNumCollisionObjects();
        for(int i = 0; i < num_obj; i++)
        {
            btCollisionObject* obj = bt_engine_dynamicsWorld->getCollisionObjectArray()[i];
            btRigidBody* body = btRigidBody::upcast(obj);
            if(body != NULL)
            {
                engine_container_p cont = (engine_container_p)body->getUserPointer();
                body->setUserPointer(NULL);

                if(cont && (cont->object_type == OBJECT_BULLET_MISC))
                {
                    if(body->getMotionState())
                    {
                        delete body->getMotionState();
                        body->setMotionState(NULL);
                    }

                    if(body->getCollisionShape())
                    {
                        delete body->getCollisionShape();
                        body->setCollisionShape(NULL);
                    }

                    bt_engine_dynamicsWorld->removeRigidBody(body);
                    cont->room = NULL;
                    free(cont);
                    delete body;
                }
            }
        }
    }
}

struct physics_data_s *Physics_CreatePhysicsData(struct engine_container_s *cont)
{
    struct physics_data_s *ret = (struct physics_data_s*)malloc(sizeof(struct physics_data_s));

    ret->bt_body = NULL;
    ret->bt_joints = NULL;
    ret->objects_count = 0;
    ret->bt_joint_count = 0;
    ret->manifoldArray = NULL;
    ret->ghostObjects = NULL;
    ret->cont = cont;

    return ret;
}


void Physics_DeletePhysicsData(struct physics_data_s *physics)
{
    if(physics)
    {
        if(physics->ghostObjects)
        {
            for(int i=0;i<physics->objects_count;i++)
            {
                physics->ghostObjects[i]->setUserPointer(NULL);
                if(physics->ghostObjects[i]->getCollisionShape())
                {
                    delete physics->ghostObjects[i]->getCollisionShape();
                    physics->ghostObjects[i]->setCollisionShape(NULL);
                }
                bt_engine_dynamicsWorld->removeCollisionObject(physics->ghostObjects[i]);
                delete physics->ghostObjects[i];
                physics->ghostObjects[i] = NULL;
            }
            free(physics->ghostObjects);
            physics->ghostObjects = NULL;
        }

        if(physics->manifoldArray)
        {
            physics->manifoldArray->clear();
            delete physics->manifoldArray;
            physics->manifoldArray = NULL;
        }

        if(physics->bt_body)
        {
            for(int i=0;i<physics->objects_count;i++)
            {
                btRigidBody *body = physics->bt_body[i];
                if(body)
                {
                    body->setUserPointer(NULL);
                    if(body->getMotionState())
                    {
                        delete body->getMotionState();
                        body->setMotionState(NULL);
                    }
                    if(body->getCollisionShape())
                    {
                        delete body->getCollisionShape();
                        body->setCollisionShape(NULL);
                    }

                    bt_engine_dynamicsWorld->removeRigidBody(body);
                    delete body;
                    physics->bt_body[i] = NULL;
                }
            }
            free(physics->bt_body);
            physics->bt_body = NULL;
        }

        physics->objects_count = 0;
        free(physics);
    }
}


/**
 * overlapping room collision filter
 */
void Physics_RoomNearCallback(btBroadphasePair& collisionPair, btCollisionDispatcher& dispatcher, const btDispatcherInfo& dispatchInfo)
{
    engine_container_p c0, c1;
    room_p r0 = NULL, r1 = NULL;

    c0 = (engine_container_p)((btCollisionObject*)collisionPair.m_pProxy0->m_clientObject)->getUserPointer();
    r0 = (c0)?(c0->room):(NULL);
    c1 = (engine_container_p)((btCollisionObject*)collisionPair.m_pProxy1->m_clientObject)->getUserPointer();
    r1 = (c1)?(c1->room):(NULL);

    if(c1 && c1 == c0)
    {
        if(((btCollisionObject*)collisionPair.m_pProxy0->m_clientObject)->isStaticOrKinematicObject() ||
           ((btCollisionObject*)collisionPair.m_pProxy1->m_clientObject)->isStaticOrKinematicObject())
        {
            return;                                                             // No self interaction
        }
        dispatcher.defaultNearCallback(collisionPair, dispatcher, dispatchInfo);
        return;
    }

    if(!r0 && !r1)
    {
        dispatcher.defaultNearCallback(collisionPair, dispatcher, dispatchInfo);// Both are out of rooms
        return;
    }

    if(r0 && r1)
    {
        if(Room_IsInNearRoomsList(r0, r1))
        {
            dispatcher.defaultNearCallback(collisionPair, dispatcher, dispatchInfo);
            return;
        }
        else
        {
            return;
        }
    }
}

/**
 * update current room of bullet object
 */
void Physics_InternalTickCallback(btDynamicsWorld *world, btScalar timeStep)
{
    /*for(int i=world->getNumCollisionObjects()-1;i>=0;i--)
    {
        btCollisionObject* obj = bt_engine_dynamicsWorld->getCollisionObjectArray()[i];
        btRigidBody* body = btRigidBody::upcast(obj);
        if (body && !body->isStaticObject() && body->getMotionState())
        {
            btTransform trans;
            body->getMotionState()->getWorldTransform(trans);
            engine_container_p cont = (engine_container_p)body->getUserPointer();
            if(cont && (cont->object_type == OBJECT_BULLET_MISC))
            {
                cont->room = Room_FindPosCogerrence(trans.getOrigin().m_floats, cont->room);
            }
        }
    }*/
}


/* Common physics functions */
void Physics_GetGravity(float g[3])
{
    btVector3 bt_g = bt_engine_dynamicsWorld->getGravity();
    vec3_copy(g, bt_g.m_floats);
}


void Physics_SetGravity(float g[3])
{
    btVector3 bt_g(g[0], g[1], g[2]);
    bt_engine_dynamicsWorld->setGravity(bt_g);
}


int  Physics_RayTest(struct collision_result_s *result, float from[3], float to[3], struct engine_container_s *cont)
{
    bt_engine_ClosestRayResultCallback cb(cont, true);
    btVector3 vFrom(from[0], from[1], from[2]), vTo(to[0], to[1], to[2]);

    cb.m_collisionFilterMask = btBroadphaseProxy::StaticFilter | btBroadphaseProxy::KinematicFilter;
    if(result)
    {
        result->obj = NULL;
        bt_engine_dynamicsWorld->rayTest(vFrom, vTo, cb);
        if(cb.hasHit())
        {
            result->obj      = (struct engine_container_s *)cb.m_collisionObject->getUserPointer();
            result->bone_num = cb.m_collisionObject->getUserIndex();
            vec3_copy(result->normale, cb.m_hitNormalWorld.m_floats);
            vFrom.setInterpolate3(vFrom, vTo, cb.m_closestHitFraction);
            vec3_copy(result->point, vFrom.m_floats);
            return 1;
        }
    }
    else
    {
        bt_engine_dynamicsWorld->rayTest(vFrom, vTo, cb);
        return cb.hasHit();
    }

    return 0;
}


int  Physics_SphereTest(struct collision_result_s *result, float from[3], float to[3], float R, struct engine_container_s *cont)
{
    bt_engine_ClosestConvexResultCallback cb(cont, true);
    btVector3 vFrom(from[0], from[1], from[2]), vTo(to[0], to[1], to[2]);
    btTransform tFrom, tTo;
    btSphereShape sphere(R);

    tFrom.setIdentity();
    tFrom.setOrigin(vFrom);
    tTo.setIdentity();
    tTo.setOrigin(vTo);

    cb.m_collisionFilterMask = btBroadphaseProxy::StaticFilter | btBroadphaseProxy::KinematicFilter;
    if(result)
    {
        result->obj = NULL;
        bt_engine_dynamicsWorld->convexSweepTest(&sphere, tFrom, tTo, cb);
        if(cb.hasHit())
        {
            result->obj      = (struct engine_container_s *)cb.m_hitCollisionObject->getUserPointer();
            result->bone_num = cb.m_hitCollisionObject->getUserIndex();
            vec3_copy(result->normale, cb.m_hitNormalWorld.m_floats);
            vec3_copy(result->point, cb.m_hitPointWorld.m_floats);
            return 1;
        }
    }
    else
    {
        bt_engine_dynamicsWorld->convexSweepTest(&sphere, tFrom, tTo, cb);
        return cb.hasHit();
    }

    return 0;
}


int Physics_IsBodyesInited(struct physics_data_s *physics)
{
    return physics && physics->bt_body;
}


int Physics_IsGhostsInited(struct physics_data_s *physics)
{
    return physics && physics->ghostObjects;
}


void Physics_GetBodyWorldTransform(struct physics_data_s *physics, float tr[16], uint16_t index)
{
    if(physics->bt_body[index])
    {
        physics->bt_body[index]->getWorldTransform().getOpenGLMatrix(tr);
    }
}


void Physics_SetBodyWorldTransform(struct physics_data_s *physics, float tr[16], uint16_t index)
{
    if(physics->bt_body[index])
    {
        physics->bt_body[index]->getWorldTransform().setFromOpenGLMatrix(tr);
    }
}


void Physics_GetGhostWorldTransform(struct physics_data_s *physics, float tr[16], uint16_t index)
{
    if(physics->ghostObjects[index])
    {
        physics->ghostObjects[index]->getWorldTransform().getOpenGLMatrix(tr);
    }
}


void Physics_SetGhostWorldTransform(struct physics_data_s *physics, float tr[16], uint16_t index)
{
    if(physics->ghostObjects[index])
    {
        physics->ghostObjects[index]->getWorldTransform().setFromOpenGLMatrix(tr);
    }
}


/**
 * It is from bullet_character_controller
 */
int Physics_GetGhostPenetrationFixVector(struct physics_data_s *physics, uint16_t index, float correction[3])
{
    // Here we must refresh the overlapping paircache as the penetrating movement itself or the
    // previous recovery iteration might have used setWorldTransform and pushed us into an object
    // that is not in the previous cache contents from the last timestep, as will happen if we
    // are pushed into a new AABB overlap. Unhandled this means the next convex sweep gets stuck.
    //
    // Do this by calling the broadphase's setAabb with the moved AABB, this will update the broadphase
    // paircache and the ghostobject's internal paircache at the same time.    /BW

    int ret = 0;
    btPairCachingGhostObject *ghost = physics->ghostObjects[index];
    if(ghost)
    {
        int num_pairs, manifolds_size;
        btBroadphasePairArray &pairArray = ghost->getOverlappingPairCache()->getOverlappingPairArray();
        btVector3 aabb_min, aabb_max, t;

        ghost->getCollisionShape()->getAabb(ghost->getWorldTransform(), aabb_min, aabb_max);
        bt_engine_dynamicsWorld->getBroadphase()->setAabb(ghost->getBroadphaseHandle(), aabb_min, aabb_max, bt_engine_dynamicsWorld->getDispatcher());
        bt_engine_dynamicsWorld->getDispatcher()->dispatchAllCollisionPairs(ghost->getOverlappingPairCache(), bt_engine_dynamicsWorld->getDispatchInfo(), bt_engine_dynamicsWorld->getDispatcher());

        vec3_set_zero(correction);
        num_pairs = ghost->getOverlappingPairCache()->getNumOverlappingPairs();
        for(int i=0;i<num_pairs;i++)
        {
            physics->manifoldArray->clear();
            // do not use commented code: it prevents to collision skips.
            //btBroadphasePair &pair = pairArray[i];
            //btBroadphasePair* collisionPair = bt_engine_dynamicsWorld->getPairCache()->findPair(pair.m_pProxy0,pair.m_pProxy1);
            btBroadphasePair *collisionPair = &pairArray[i];

            if(!collisionPair)
            {
                continue;
            }

            if(collisionPair->m_algorithm)
            {
                collisionPair->m_algorithm->getAllContactManifolds(*(physics->manifoldArray));
            }

            manifolds_size = physics->manifoldArray->size();
            for(int j=0;j<manifolds_size;j++)
            {
                btPersistentManifold* manifold = (*(physics->manifoldArray))[j];
                btScalar directionSign = manifold->getBody0() == ghost ? btScalar(-1.0) : btScalar(1.0);
                engine_container_p cont0 = (engine_container_p)manifold->getBody0()->getUserPointer();
                engine_container_p cont1 = (engine_container_p)manifold->getBody1()->getUserPointer();
                if((cont0->collision_type == COLLISION_TYPE_GHOST) || (cont1->collision_type == COLLISION_TYPE_GHOST))
                {
                    continue;
                }
                for(int k=0;k<manifold->getNumContacts();k++)
                {
                    const btManifoldPoint&pt = manifold->getContactPoint(k);
                    btScalar dist = pt.getDistance();

                    if(dist < 0.0)
                    {
                        t = pt.m_normalWorldOnB * dist * directionSign;
                        vec3_add(correction, correction, t.m_floats)
                        ret++;
                    }
                }
            }
        }
    }

    return ret;
}


btCollisionShape *BT_CSfromBBox(btScalar *bb_min, btScalar *bb_max)
{
    obb_p obb = OBB_Create();
    polygon_p p = obb->base_polygons;
    btTriangleMesh *trimesh = new btTriangleMesh;
    btVector3 v0, v1, v2;
    btCollisionShape* ret;
    int cnt = 0;

    OBB_Rebuild(obb, bb_min, bb_max);
    for(uint16_t i=0;i<6;i++,p++)
    {
        if(Polygon_IsBroken(p))
        {
            continue;
        }
        for(uint16_t j=1;j+1<p->vertex_count;j++)
        {
            vec3_copy(v0.m_floats, p->vertices[j + 1].position);
            vec3_copy(v1.m_floats, p->vertices[j].position);
            vec3_copy(v2.m_floats, p->vertices[0].position);
            trimesh->addTriangle(v0, v1, v2, true);
        }
        cnt ++;
    }

    OBB_Clear(obb);
    free(obb);

    if(cnt == 0)                                                                // fixed: without that condition engine may easily crash
    {
        delete trimesh;
        return NULL;
    }

    ret = new btConvexTriangleMeshShape(trimesh, true);

    return ret;
}


btCollisionShape *BT_CSfromMesh(struct base_mesh_s *mesh, bool useCompression, bool buildBvh, bool is_static)
{
    uint32_t cnt = 0;
    polygon_p p;
    btTriangleMesh *trimesh = new btTriangleMesh;
    btCollisionShape* ret = NULL;
    btVector3 v0, v1, v2;

    p = mesh->polygons;
    for(uint32_t i=0;i<mesh->polygons_count;i++,p++)
    {
        if(Polygon_IsBroken(p))
        {
            continue;
        }

        for(uint16_t j=1;j+1<p->vertex_count;j++)
        {
            vec3_copy(v0.m_floats, p->vertices[j + 1].position);
            vec3_copy(v1.m_floats, p->vertices[j].position);
            vec3_copy(v2.m_floats, p->vertices[0].position);
            trimesh->addTriangle(v0, v1, v2, true);
        }
        cnt ++;
    }

    if(cnt == 0)
    {
        delete trimesh;
        return NULL;
    }

    if(is_static)
    {
        ret = new btBvhTriangleMeshShape(trimesh, useCompression, buildBvh);
    }
    else
    {
        ret = new btConvexTriangleMeshShape(trimesh, true);
    }

    return ret;
}

///@TODO: resolve cases with floor >> ceiling (I.E. floor - ceiling >= 2048)
btCollisionShape *BT_CSfromHeightmap(struct room_sector_s *heightmap, struct sector_tween_s *tweens, int tweens_size, bool useCompression, bool buildBvh)
{
    uint32_t cnt = 0;
    room_p r = heightmap->owner_room;
    btTriangleMesh *trimesh = new btTriangleMesh;
    btCollisionShape* ret = NULL;

    for(uint32_t i = 0; i < r->sectors_count; i++)
    {
        if( (heightmap[i].floor_penetration_config != TR_PENETRATION_CONFIG_GHOST) &&
            (heightmap[i].floor_penetration_config != TR_PENETRATION_CONFIG_WALL )  )
        {
            if( (heightmap[i].floor_diagonal_type == TR_SECTOR_DIAGONAL_TYPE_NONE) ||
                (heightmap[i].floor_diagonal_type == TR_SECTOR_DIAGONAL_TYPE_NW  )  )
            {
                if(heightmap[i].floor_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_A)
                {
                    btScalar *v0 = heightmap[i].floor_corners[3];
                    btScalar *v1 = heightmap[i].floor_corners[2];
                    btScalar *v2 = heightmap[i].floor_corners[0];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         true);
                    cnt++;
                }

                if(heightmap[i].floor_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_B)
                {
                    btScalar *v0 = heightmap[i].floor_corners[2];
                    btScalar *v1 = heightmap[i].floor_corners[1];
                    btScalar *v2 = heightmap[i].floor_corners[0];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         true);
                    cnt++;
                }
            }
            else
            {
                if(heightmap[i].floor_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_A)
                {
                    btScalar *v0 = heightmap[i].floor_corners[3];
                    btScalar *v1 = heightmap[i].floor_corners[2];
                    btScalar *v2 = heightmap[i].floor_corners[1];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         true);
                    cnt++;
                }

                if(heightmap[i].floor_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_B)
                {
                    btScalar *v0 = heightmap[i].floor_corners[3];
                    btScalar *v1 = heightmap[i].floor_corners[1];
                    btScalar *v2 = heightmap[i].floor_corners[0];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         true);
                    cnt++;
                }
            }
        }

        if( (heightmap[i].ceiling_penetration_config != TR_PENETRATION_CONFIG_GHOST) &&
            (heightmap[i].ceiling_penetration_config != TR_PENETRATION_CONFIG_WALL )  )
        {
            if( (heightmap[i].ceiling_diagonal_type == TR_SECTOR_DIAGONAL_TYPE_NONE) ||
                (heightmap[i].ceiling_diagonal_type == TR_SECTOR_DIAGONAL_TYPE_NW  )  )
            {
                if(heightmap[i].ceiling_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_A)
                {
                    btScalar *v0 = heightmap[i].ceiling_corners[0];
                    btScalar *v1 = heightmap[i].ceiling_corners[2];
                    btScalar *v2 = heightmap[i].ceiling_corners[3];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         true);
                    cnt++;
                }

                if(heightmap[i].ceiling_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_B)
                {
                    btScalar *v0 = heightmap[i].ceiling_corners[0];
                    btScalar *v1 = heightmap[i].ceiling_corners[1];
                    btScalar *v2 = heightmap[i].ceiling_corners[2];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         true);
                    cnt++;
                }
            }
            else
            {
                if(heightmap[i].ceiling_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_A)
                {
                    btScalar *v0 = heightmap[i].ceiling_corners[0];
                    btScalar *v1 = heightmap[i].ceiling_corners[1];
                    btScalar *v2 = heightmap[i].ceiling_corners[3];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         true);
                    cnt++;
                }

                if(heightmap[i].ceiling_penetration_config != TR_PENETRATION_CONFIG_DOOR_VERTICAL_B)
                {
                    btScalar *v0 = heightmap[i].ceiling_corners[1];
                    btScalar *v1 = heightmap[i].ceiling_corners[2];
                    btScalar *v2 = heightmap[i].ceiling_corners[3];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         true);
                    cnt++;
                }
            }
        }
    }

    for(int i=0; i<tweens_size; i++)
    {
        switch(tweens[i].ceiling_tween_type)
        {
            case TR_SECTOR_TWEEN_TYPE_2TRIANGLES:
                {
                    btScalar t = fabs((tweens[i].ceiling_corners[2][2] - tweens[i].ceiling_corners[3][2]) /
                                      (tweens[i].ceiling_corners[0][2] - tweens[i].ceiling_corners[1][2]));
                    t = 1.0 / (1.0 + t);
                    btScalar o[3], t1 = 1.0 - t;
                    btScalar *v0 = tweens[i].ceiling_corners[0];
                    btScalar *v1 = tweens[i].ceiling_corners[1];
                    btScalar *v2 = tweens[i].ceiling_corners[2];
                    btScalar *v3 = tweens[i].ceiling_corners[3];
                    vec3_interpolate_macro(o, v0, v2, t, t1);

                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(o[0], o[1], o[2]),
                                         true);
                    trimesh->addTriangle(btVector3(v3[0], v3[1], v3[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         btVector3(o[0], o[1], o[2]),
                                         true);
                    cnt += 2;
                }
                break;

            case TR_SECTOR_TWEEN_TYPE_TRIANGLE_LEFT:
                {
                    btScalar *v0 = tweens[i].ceiling_corners[0];
                    btScalar *v1 = tweens[i].ceiling_corners[1];
                    btScalar *v2 = tweens[i].ceiling_corners[3];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         true);
                    cnt++;
                }
                break;

            case TR_SECTOR_TWEEN_TYPE_TRIANGLE_RIGHT:
                {
                    btScalar *v0 = tweens[i].ceiling_corners[2];
                    btScalar *v1 = tweens[i].ceiling_corners[1];
                    btScalar *v2 = tweens[i].ceiling_corners[3];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         true);
                    cnt++;
                }
                break;

            case TR_SECTOR_TWEEN_TYPE_QUAD:
                {
                    btScalar *v0 = tweens[i].ceiling_corners[0];
                    btScalar *v1 = tweens[i].ceiling_corners[1];
                    btScalar *v2 = tweens[i].ceiling_corners[2];
                    btScalar *v3 = tweens[i].ceiling_corners[3];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v3[0], v3[1], v3[2]),
                                         true);
                    trimesh->addTriangle(btVector3(v2[0], v2[1], v2[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v3[0], v3[1], v3[2]),
                                         true);
                    cnt += 2;
                }
                break;
        };

        switch(tweens[i].floor_tween_type)
        {
            case TR_SECTOR_TWEEN_TYPE_2TRIANGLES:
                {
                    btScalar t = fabs((tweens[i].floor_corners[2][2] - tweens[i].floor_corners[3][2]) /
                                      (tweens[i].floor_corners[0][2] - tweens[i].floor_corners[1][2]));
                    t = 1.0 / (1.0 + t);
                    btScalar o[3], t1 = 1.0 - t;
                    btScalar *v0 = tweens[i].floor_corners[0];
                    btScalar *v1 = tweens[i].floor_corners[1];
                    btScalar *v2 = tweens[i].floor_corners[2];
                    btScalar *v3 = tweens[i].floor_corners[3];
                    vec3_interpolate_macro(o, v0, v2, t, t1);

                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(o[0], o[1], o[2]),
                                         true);
                    trimesh->addTriangle(btVector3(v3[0], v3[1], v3[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         btVector3(o[0], o[1], o[2]),
                                         true);
                    cnt += 2;
                }
                break;

            case TR_SECTOR_TWEEN_TYPE_TRIANGLE_LEFT:
                {
                    btScalar *v0 = tweens[i].floor_corners[0];
                    btScalar *v1 = tweens[i].floor_corners[1];
                    btScalar *v2 = tweens[i].floor_corners[3];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         true);
                    cnt++;
                }
                break;

            case TR_SECTOR_TWEEN_TYPE_TRIANGLE_RIGHT:
                {
                    btScalar *v0 = tweens[i].floor_corners[2];
                    btScalar *v1 = tweens[i].floor_corners[1];
                    btScalar *v2 = tweens[i].floor_corners[3];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v2[0], v2[1], v2[2]),
                                         true);
                    cnt++;
                }
                break;

            case TR_SECTOR_TWEEN_TYPE_QUAD:
                {
                    btScalar *v0 = tweens[i].floor_corners[0];
                    btScalar *v1 = tweens[i].floor_corners[1];
                    btScalar *v2 = tweens[i].floor_corners[2];
                    btScalar *v3 = tweens[i].floor_corners[3];
                    trimesh->addTriangle(btVector3(v0[0], v0[1], v0[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v3[0], v3[1], v3[2]),
                                         true);
                    trimesh->addTriangle(btVector3(v2[0], v2[1], v2[2]),
                                         btVector3(v1[0], v1[1], v1[2]),
                                         btVector3(v3[0], v3[1], v3[2]),
                                         true);
                    cnt += 2;
                }
                break;
        };
    }

    if(cnt == 0)
    {
        delete trimesh;
        return NULL;
    }

    ret = new btBvhTriangleMeshShape(trimesh, useCompression, buildBvh);
    return ret;
}

/*
 * =============================================================================
 */

void Physics_GenRigidBody(struct physics_data_s *physics, struct ss_bone_frame_s *bf, float transform[16])
{
    btScalar tr[16];
    btVector3 localInertia(0, 0, 0);
    btTransform startTransform;

    if(bf->animations.model == NULL)
    {
        return;
    }

    physics->objects_count = bf->bone_tag_count;
    physics->bt_body = (btRigidBody**)malloc(physics->objects_count * sizeof(btRigidBody*));

    for(uint16_t i=0;i<physics->objects_count;i++)
    {
        base_mesh_p mesh = bf->animations.model->mesh_tree[i].mesh_base;
        btCollisionShape *cshape = NULL;
        switch(physics->cont->collision_shape)
        {
            case COLLISION_SHAPE_TRIMESH_CONVEX:
                cshape = BT_CSfromMesh(mesh, true, true, false);
                break;

            case COLLISION_SHAPE_TRIMESH:
                cshape = BT_CSfromMesh(mesh, true, true, true);
                break;

            case COLLISION_SHAPE_BOX:
                cshape = BT_CSfromBBox(mesh->bb_min, mesh->bb_max);
                break;

                ///@TODO: add other shapes implementation; may be change default;
            default:
                 cshape = BT_CSfromMesh(mesh, true, true, true);
                 break;
        };

        physics->bt_body[i] = NULL;

        if(cshape)
        {
            cshape->calculateLocalInertia(0.0, localInertia);

            Mat4_Mat4_mul(tr, transform, bf->bone_tags[i].full_transform);
            startTransform.setFromOpenGLMatrix(tr);
            btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
            physics->bt_body[i] = new btRigidBody(0.0, motionState, cshape, localInertia);
            //ent->physics->bt_body[i]->setCollisionFlags(ent->physics->bt_body[i]->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
            bt_engine_dynamicsWorld->addRigidBody(physics->bt_body[i], COLLISION_GROUP_KINEMATIC, COLLISION_MASK_ALL);
            physics->bt_body[i]->setUserPointer(physics->cont);
            physics->bt_body[i]->setUserIndex(i);
        }
    }
}

/*
 * DO something with sticky 80% boxes!!!
 * first of all: convex offsetted boxes to avoid every frame offsets calculation.
 */
void Physics_CreateGhosts(struct physics_data_s *physics, struct ss_bone_frame_s *bf, float transform[16])
{
    if(physics->objects_count > 0)
    {
        btTransform tr;
        btScalar gltr[16], v[3];

        physics->manifoldArray = new btManifoldArray();
        physics->ghostObjects = (btPairCachingGhostObject**)malloc(bf->bone_tag_count * sizeof(btPairCachingGhostObject*));
        for(uint16_t i = 0; i < physics->objects_count; i++)
        {
            btVector3 box;
            box.m_floats[0] = 0.40 * (bf->bone_tags[i].mesh_base->bb_max[0] - bf->bone_tags[i].mesh_base->bb_min[0]);
            box.m_floats[1] = 0.40 * (bf->bone_tags[i].mesh_base->bb_max[1] - bf->bone_tags[i].mesh_base->bb_min[1]);
            box.m_floats[2] = 0.40 * (bf->bone_tags[i].mesh_base->bb_max[2] - bf->bone_tags[i].mesh_base->bb_min[2]);
            bf->bone_tags[i].mesh_base->R = (box.m_floats[0] < box.m_floats[1])?(box.m_floats[0]):(box.m_floats[1]);
            bf->bone_tags[i].mesh_base->R = (bf->bone_tags[i].mesh_base->R < box.m_floats[2])?(bf->bone_tags[i].mesh_base->R):(box.m_floats[2]);

            physics->ghostObjects[i] = new btPairCachingGhostObject();
            physics->ghostObjects[i]->setIgnoreCollisionCheck(physics->bt_body[i], true);
            Mat4_Mat4_mul(gltr, transform, bf->bone_tags[i].full_transform);
            Mat4_vec3_mul(v, gltr, bf->bone_tags[i].mesh_base->centre);
            vec3_copy(gltr+12, v);
            tr.setFromOpenGLMatrix(gltr);
            physics->ghostObjects[i]->setWorldTransform(tr);
            physics->ghostObjects[i]->setCollisionFlags(physics->ghostObjects[i]->getCollisionFlags() | btCollisionObject::CF_CHARACTER_OBJECT);
            physics->ghostObjects[i]->setUserPointer(physics->cont);
            physics->ghostObjects[i]->setCollisionShape(new btBoxShape(box));
            bt_engine_dynamicsWorld->addCollisionObject(physics->ghostObjects[i], COLLISION_GROUP_CHARACTERS, COLLISION_GROUP_ALL);
        }
    }
}


void Physics_GenStaticMeshRigidBody(struct static_mesh_s *smesh)
{
    btCollisionShape *cshape = NULL;

    smesh->physics_body = NULL;
    switch(smesh->self->collision_shape)
    {
        case COLLISION_SHAPE_BOX:
            cshape = BT_CSfromBBox(smesh->cbb_min, smesh->cbb_max);
            break;

        case COLLISION_SHAPE_BOX_BASE:
            cshape = BT_CSfromBBox(smesh->mesh->bb_min, smesh->mesh->bb_max);
            break;

        case COLLISION_SHAPE_TRIMESH:
            cshape = BT_CSfromMesh(smesh->mesh, true, true, true);
            break;

        case COLLISION_SHAPE_TRIMESH_CONVEX:
            cshape = BT_CSfromMesh(smesh->mesh, true, true, false);
            break;

        default:
            cshape = NULL;
            break;
    };

    if(cshape)
    {
        btVector3 localInertia(0, 0, 0);
        btTransform startTransform;
        startTransform.setFromOpenGLMatrix(smesh->transform);
        smesh->physics_body = (struct physics_object_s*)malloc(sizeof(struct physics_object_s));
        btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
        smesh->physics_body->bt_body = new btRigidBody(0.0, motionState, cshape, localInertia);
        bt_engine_dynamicsWorld->addRigidBody(smesh->physics_body->bt_body, COLLISION_GROUP_ALL, COLLISION_MASK_ALL);
        smesh->physics_body->bt_body->setUserPointer(smesh->self);
    }
}


void Physics_GenRoomRigidBody(struct room_s *room, struct sector_tween_s *tweens, int num_tweens)
{
    btCollisionShape *cshape = BT_CSfromHeightmap(room->sectors, tweens, num_tweens, true, true);
    room->physics_body = NULL;

    if(cshape)
    {
        btVector3 localInertia(0, 0, 0);
        btTransform tr;
        tr.setFromOpenGLMatrix(room->transform);
        room->physics_body = (struct physics_object_s*)malloc(sizeof(struct physics_object_s));
        btDefaultMotionState* motionState = new btDefaultMotionState(tr);
        room->physics_body->bt_body = new btRigidBody(0.0, motionState, cshape, localInertia);
        bt_engine_dynamicsWorld->addRigidBody(room->physics_body->bt_body, COLLISION_GROUP_ALL, COLLISION_MASK_ALL);
        room->physics_body->bt_body->setUserPointer(room->self);
        room->physics_body->bt_body->setUserIndex(0);
        room->physics_body->bt_body->setRestitution(1.0);
        room->physics_body->bt_body->setFriction(1.0);
        room->self->collision_type = COLLISION_TYPE_STATIC;                     // meshtree
        room->self->collision_shape = COLLISION_SHAPE_TRIMESH;
    }
}


void Physics_DeleteObject(struct physics_object_s *obj)
{
    if(obj)
    {
        obj->bt_body->setUserPointer(NULL);
        if(obj->bt_body->getMotionState())
        {
            delete obj->bt_body->getMotionState();
            obj->bt_body->setMotionState(NULL);
        }
        if(obj->bt_body->getCollisionShape())
        {
            delete obj->bt_body->getCollisionShape();
            obj->bt_body->setCollisionShape(NULL);
        }

        bt_engine_dynamicsWorld->removeRigidBody(obj->bt_body);
        delete obj->bt_body;
        free(obj);
    }
}


void Physics_EnableObject(struct physics_object_s *obj)
{
    bt_engine_dynamicsWorld->addRigidBody(obj->bt_body);
}


void Physics_DisableObject(struct physics_object_s *obj)
{
    bt_engine_dynamicsWorld->removeRigidBody(obj->bt_body);
}


/**
 * This function enables collision for entity_p in all cases exept NULL models.
 * If collision models does not exists, function will create them;
 * @param ent - pointer to the entity.
 */
void Physics_EnableCollision(struct physics_data_s *physics)
{
    if(physics->bt_body != NULL)
    {
        for(uint16_t i=0;i<physics->objects_count;i++)
        {
            btRigidBody *b = physics->bt_body[i];
            if((b != NULL) && !b->isInWorld())
            {
                bt_engine_dynamicsWorld->addRigidBody(b);
            }
        }
    }
}


void Physics_DisableCollision(struct physics_data_s *physics)
{
    if(physics->bt_body != NULL)
    {
        for(uint16_t i=0;i<physics->objects_count;i++)
        {
            btRigidBody *b = physics->bt_body[i];
            if((b != NULL) && b->isInWorld())
            {
                bt_engine_dynamicsWorld->removeRigidBody(b);
            }
        }
    }
}


void Physics_SetCollisionScale(struct physics_data_s *physics, float scaling[3])
{
    for(int i=0; i<physics->objects_count; i++)
    {
        bt_engine_dynamicsWorld->removeRigidBody(physics->bt_body[i]);
            physics->bt_body[i]->getCollisionShape()->setLocalScaling(btVector3(scaling[0], scaling[1], scaling[2]));
        bt_engine_dynamicsWorld->addRigidBody(physics->bt_body[i]);

        physics->bt_body[i]->activate();
    }
}


void Physics_SetBodyMass(struct physics_data_s *physics, float mass, uint16_t index)
{
    btVector3 inertia (0.0, 0.0, 0.0);
    bt_engine_dynamicsWorld->removeRigidBody(physics->bt_body[index]);

        physics->bt_body[index]->getCollisionShape()->calculateLocalInertia(mass, inertia);

        physics->bt_body[index]->setMassProps(mass, inertia);

        physics->bt_body[index]->updateInertiaTensor();
        physics->bt_body[index]->clearForces();

        btVector3 factor = (mass > 0.0)?(btVector3(1.0, 1.0, 1.0)):(btVector3(0.0, 0.0, 0.0));
        physics->bt_body[index]->setLinearFactor (factor);
        physics->bt_body[index]->setAngularFactor(factor);

        //physics_body[index]->forceActivationState(DISABLE_DEACTIVATION);

        //physics_body[index]->setCcdMotionThreshold(32.0);   // disable tunneling effect
        //physics_body[index]->setCcdSweptSphereRadius(32.0);

    bt_engine_dynamicsWorld->addRigidBody(physics->bt_body[index]);

    physics->bt_body[index]->activate();
}


void Physics_PushBody(struct physics_data_s *physics, float speed[3], uint16_t index)
{
    physics->bt_body[index]->setLinearVelocity(btVector3(speed[0], speed[1], speed[2]));
}


void Physics_SetLinearFactor(struct physics_data_s *physics, float factor[3], uint16_t index)
{
    physics->bt_body[index]->setLinearFactor(btVector3(factor[0], factor[1], factor[2]));
}


struct collision_node_s *Physics_GetCurrentCollisions(struct physics_data_s *physics)
{
    struct collision_node_s *ret = NULL;

    if(physics->ghostObjects)
    {
        btTransform orig_tr;
        for(uint16_t i=0;i<physics->objects_count;i++)
        {
            btPairCachingGhostObject *ghost = physics->ghostObjects[i];
            btBroadphasePairArray &pairArray = ghost->getOverlappingPairCache()->getOverlappingPairArray();
            btVector3 aabb_min, aabb_max;

            ghost->getCollisionShape()->getAabb(ghost->getWorldTransform(), aabb_min, aabb_max);
            bt_engine_dynamicsWorld->getBroadphase()->setAabb(ghost->getBroadphaseHandle(), aabb_min, aabb_max, bt_engine_dynamicsWorld->getDispatcher());
            bt_engine_dynamicsWorld->getDispatcher()->dispatchAllCollisionPairs(ghost->getOverlappingPairCache(), bt_engine_dynamicsWorld->getDispatchInfo(), bt_engine_dynamicsWorld->getDispatcher());

            int num_pairs = ghost->getOverlappingPairCache()->getNumOverlappingPairs();
            for(int j=0;j<num_pairs;j++)
            {
                physics->manifoldArray->clear();
                btBroadphasePair *collisionPair = &pairArray[j];

                if(!collisionPair)
                {
                    continue;
                }

                if(collisionPair->m_algorithm)
                {
                    collisionPair->m_algorithm->getAllContactManifolds(*physics->manifoldArray);
                }

                for(int k=0;k<physics->manifoldArray->size();k++)
                {
                    btPersistentManifold* manifold = (*physics->manifoldArray)[k];
                    for(int c=0;c<manifold->getNumContacts();c++)               // c++ in C++
                    {
                        //const btManifoldPoint &pt = manifold->getContactPoint(c);
                        if(manifold->getContactPoint(c).getDistance() < 0.0)
                        {
                            collision_node_p cn = Physics_GetCollisionNode();
                            if(cn == NULL)
                            {
                                break;
                            }
                            btCollisionObject *obj = (btCollisionObject*)(*physics->manifoldArray)[k]->getBody0();
                            cn->obj = (engine_container_p)obj->getUserPointer();
                            if(physics->cont == cn->obj)
                            {
                                obj = (btCollisionObject*)(*physics->manifoldArray)[k]->getBody1();
                                cn->obj = (engine_container_p)obj->getUserPointer();
                            }
                            cn->part_from = obj->getUserIndex();
                            cn->part_self = i;
                            cn->next = ret;
                            ret = cn;
                            break;
                        }
                    }
                }
            }
            ghost->setWorldTransform(orig_tr);
        }
    }

    return ret;
}

/* *****************************************************************************
 * ************************  HAIR DATA  ****************************************
 * ****************************************************************************/

typedef struct hair_element_s
{
    struct base_mesh_s         *mesh;              // Pointer to rendered mesh.
    btCollisionShape           *shape;             // Pointer to collision shape.
    btRigidBody                *body;              // Pointer to dynamic body.
    btGeneric6DofConstraint    *joint;             // Array of joints.
}hair_element_t, *hair_element_p;


typedef struct hair_s
{
    engine_container_p        container;
    uint32_t                  owner_body;         // Owner entity's body ID.

    uint8_t                   root_index;         // Index of "root" element.
    uint8_t                   tail_index;         // Index of "tail" element.

    uint8_t                   element_count;      // Overall amount of elements.
    hair_element_s           *elements;           // Array of elements.

    uint8_t                   vertex_map_count;
    uint32_t                 *hair_vertex_map;    // Hair vertex indices to link
    uint32_t                 *head_vertex_map;    // Head vertex indices to link

}hair_t, *hair_p;

typedef struct hair_setup_s
{
    uint32_t     model_id;           // Hair model ID
    uint32_t     link_body;          // Lara's head mesh index

    btScalar     root_weight;        // Root and tail hair body weight. Intermediate body
    btScalar     tail_weight;        // weights are calculated from these two parameters

    btScalar     hair_damping[2];    // Damping affects hair "plasticity"
    btScalar     hair_inertia;       // Inertia affects hair "responsiveness"
    btScalar     hair_restitution;   // "Bounciness" of the hair
    btScalar     hair_friction;      // How much other bodies will affect hair trajectory

    btScalar     joint_overlap;      // How much two hair bodies overlap each other

    btScalar     joint_cfm;          // Constraint force mixing (joint softness)
    btScalar     joint_erp;          // Error reduction parameter (joint "inertia")

    btScalar     head_offset[3];     // Linear offset to place hair to
    btScalar     root_angle[3];      // First constraint set angle (to align hair angle)

    uint32_t     vertex_map_count;   // Amount of REAL vertices to link head and hair
    uint32_t     hair_vertex_map[HAIR_VERTEX_MAP_LIMIT]; // Hair vertex indices to link
    uint32_t     head_vertex_map[HAIR_VERTEX_MAP_LIMIT]; // Head vertex indices to link

}hair_setup_t, *hair_setup_p;


struct hair_s *Hair_Create(struct hair_setup_s *setup, struct physics_data_s *physics)
{
    // No setup or parent to link to - bypass function.

    if(!physics || !setup || (setup->link_body >= physics->objects_count) ||
       !physics->bt_body[setup->link_body])
    {
        return NULL;
    }

    skeletal_model_p model = World_GetModelByID(&engine_world, setup->model_id);
    if((!model) || (model->mesh_count == 0))
    {
        return NULL;
    }

    // Setup engine container. FIXME: DOESN'T WORK PROPERLY ATM.

    struct hair_s *hair = (struct hair_s*)calloc(1, sizeof(struct hair_s));
    hair->container = Container_Create();
    hair->container->room = physics->cont->room;
    hair->container->object_type = OBJECT_HAIR;
    hair->container->object = hair;

    // Setup initial hair parameters.
    hair->owner_body = setup->link_body;    // Entity body to refer to.

    // Setup initial position / angles.
    btTransform startTransform = physics->bt_body[hair->owner_body]->getWorldTransform();
    // Number of elements (bodies) is equal to number of hair meshes.

    hair->element_count = model->mesh_count;
    hair->elements      = (hair_element_p)calloc(hair->element_count, sizeof(hair_element_t));

    // Root index should be always zero, as it is how engine determines that it is
    // connected to head and renders it properly. Tail index should be always the
    // last element of the hair, as it indicates absence of "child" constraint.

    hair->root_index = 0;
    hair->tail_index = hair->element_count-1;

    // Weight step is needed to determine the weight of each hair body.
    // It is derived from root body weight and tail body weight.

    btScalar weight_step = ((setup->root_weight - setup->tail_weight) / hair->element_count);
    btScalar current_weight = setup->root_weight;

    for(uint8_t i=0;i<hair->element_count;i++)
    {
        // Point to corresponding mesh.

        hair->elements[i].mesh = model->mesh_tree[i].mesh_base;

        // Begin creating ACTUAL physical hair mesh.
        btVector3   localInertia(0, 0, 0);

        // Make collision shape out of mesh.
        hair->elements[i].shape = BT_CSfromMesh(hair->elements[i].mesh, true, true, false);
        hair->elements[i].shape->calculateLocalInertia((current_weight * setup->hair_inertia), localInertia);
        hair->elements[i].joint = NULL;

        // Decrease next body weight to weight_step parameter.
        current_weight -= weight_step;

        // Initialize motion state for body.
        btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);

        // Make rigid body.
        hair->elements[i].body = new btRigidBody(current_weight, motionState, hair->elements[i].shape, localInertia);

        // Damping makes body stop in space by itself, to prevent it from continous movement.
        hair->elements[i].body->setDamping(setup->hair_damping[0], setup->hair_damping[1]);

        // Restitution and friction parameters define "bounciness" and "dullness" of hair.
        hair->elements[i].body->setRestitution(setup->hair_restitution);
        hair->elements[i].body->setFriction(setup->hair_friction);

        // Since hair is always moving with Lara, even if she's in still state (like, hanging
        // on a ledge), hair bodies shouldn't deactivate over time.
        hair->elements[i].body->forceActivationState(DISABLE_DEACTIVATION);

        // Hair bodies must not collide with each other, and also collide ONLY with kinematic
        // bodies (e. g. animated meshes), or else Lara's ghost object or anything else will be able to
        // collide with hair!
        hair->elements[i].body->setUserPointer(hair->container);
        bt_engine_dynamicsWorld->addRigidBody(hair->elements[i].body, COLLISION_GROUP_CHARACTERS, COLLISION_GROUP_KINEMATIC);

        hair->elements[i].body->activate();
    }

    // GENERATE CONSTRAINTS.
    // All constraints are generic 6-DOF type, as they seem perfect fit for hair.

    // If multiple joints per body is specified, joints are placed in circular manner,
    // with obvious step of (SIMD_2_PI) / joint count. It means that all joints will form
    // circle-like figure.

    for(uint16_t i=0; i<hair->element_count; i++)
    {
        btRigidBody* prev_body;
        btScalar     body_length;

        // Each body width and height are used to calculate position of each joint.

        //btScalar body_width = fabs(hair->elements[i].mesh->bb_max[0] - hair->elements[i].mesh->bb_min[0]);
        //btScalar body_depth = fabs(hair->elements[i].mesh->bb_max[3] - hair->elements[i].mesh->bb_min[3]);

        btTransform localA; localA.setIdentity();
        btTransform localB; localB.setIdentity();

        btScalar joint_x = 0.0;
        btScalar joint_y = 0.0;

        if(i == 0)  // First joint group
        {
            // Adjust pivot point A to parent body.
            localA.setOrigin(btVector3(setup->head_offset[0] + joint_x, setup->head_offset[1], setup->head_offset[2] + joint_y));
            localA.getBasis().setEulerZYX(setup->root_angle[0], setup->root_angle[1], setup->root_angle[2]);

            localB.setOrigin(btVector3(joint_x, 0.0, joint_y));
            localB.getBasis().setEulerZYX(0,-SIMD_HALF_PI,0);

            prev_body = physics->bt_body[hair->owner_body];                     // Previous body is parent body.
        }
        else
        {
            // Adjust pivot point A to previous mesh's length, considering mesh overlap multiplier.

            body_length = fabs(hair->elements[i-1].mesh->bb_max[1] - hair->elements[i-1].mesh->bb_min[1]) * setup->joint_overlap;

            localA.setOrigin(btVector3(joint_x, body_length, joint_y));
            localA.getBasis().setEulerZYX(0,SIMD_HALF_PI,0);

            // Pivot point B is automatically adjusted by Bullet.

            localB.setOrigin(btVector3(joint_x, 0.0, joint_y));
            localB.getBasis().setEulerZYX(0,SIMD_HALF_PI,0);

            prev_body = hair->elements[i-1].body;   // Previous body is preceiding hair mesh.
        }

        // Create 6DOF constraint.
        hair->elements[i].joint = new btGeneric6DofConstraint(*prev_body, *(hair->elements[i].body), localA, localB, true);

        // CFM and ERP parameters are critical for making joint "hard" and link
        // to Lara's head. With wrong values, constraints may become "elastic".
        for(int axis=0;axis<=5;axis++)
        {
            hair->elements[i].joint->setParam(BT_CONSTRAINT_STOP_CFM, setup->joint_cfm, axis);
            hair->elements[i].joint->setParam(BT_CONSTRAINT_STOP_ERP, setup->joint_erp, axis);
        }

        if(i == 0)
        {
            // First joint group should be more limited in motion, as it is connected
            // right to the head. NB: Should we make it scriptable as well?
            hair->elements[i].joint->setLinearLowerLimit(btVector3(0., 0., 0.));
            hair->elements[i].joint->setLinearUpperLimit(btVector3(0., 0., 0.));
            hair->elements[i].joint->setAngularLowerLimit(btVector3(-SIMD_HALF_PI,     0., -SIMD_HALF_PI*0.4));
            hair->elements[i].joint->setAngularUpperLimit(btVector3(-SIMD_HALF_PI*0.3, 0.,  SIMD_HALF_PI*0.4));

            // Increased solver iterations make constraint even more stable.
            hair->elements[i].joint->setOverrideNumSolverIterations(100);
        }
        else
        {
            // Normal joint with more movement freedom.
            hair->elements[i].joint->setLinearLowerLimit(btVector3(0., 0., 0.));
            hair->elements[i].joint->setLinearUpperLimit(btVector3(0., 0., 0.));
            hair->elements[i].joint->setAngularLowerLimit(btVector3(-SIMD_HALF_PI*0.5, 0., -SIMD_HALF_PI*0.5));
            hair->elements[i].joint->setAngularUpperLimit(btVector3( SIMD_HALF_PI*0.5, 0.,  SIMD_HALF_PI*0.5));
        }
        hair->elements[i].joint->setDbgDrawSize(btScalar(5.f));    // Draw constraint axes.

        // Add constraint to the world.
        bt_engine_dynamicsWorld->addConstraint(hair->elements[i].joint, true);
    }

    return hair;
}

void Hair_Delete(struct hair_s *hair)
{
    if(hair)
    {
        for(int i=0; i<hair->element_count; i++)
        {
            if(hair->elements[i].joint)
            {
                bt_engine_dynamicsWorld->removeConstraint(hair->elements[i].joint);
                delete hair->elements[i].joint;
                hair->elements[i].joint = NULL;
            }
        }

        for(int i=0; i<hair->element_count; i++)
        {
            if(hair->elements[i].body)
            {
                hair->elements[i].body->setUserPointer(NULL);
                bt_engine_dynamicsWorld->removeRigidBody(hair->elements[i].body);
                delete hair->elements[i].body;
                hair->elements[i].body = NULL;
            }
            if(hair->elements[i].shape)
            {
                delete hair->elements[i].shape;
                hair->elements[i].shape = NULL;
            }
        }
        free(hair->elements);
        hair->elements = NULL;
        hair->element_count = 0;

        free(hair->container);
        hair->container = NULL;
        hair->owner_body = 0;

        hair->root_index = 0;
        hair->tail_index = 0;
        free(hair);
    }
}


void Hair_Update(struct hair_s *hair, struct physics_data_s *physics)
{
    if(hair && (hair->element_count > 0))
    {
        /*btScalar new_transform[16];

        Mat4_Mat4_mul(new_transform, entity->transform, entity->bf->bone_tags[hair->owner_body].full_transform);

        // Calculate mixed velocities.
        btVector3 mix_vel(new_transform[12+0] - hair->owner_body_transform[12+0],
                          new_transform[12+1] - hair->owner_body_transform[12+1],
                          new_transform[12+2] - hair->owner_body_transform[12+2]);
        mix_vel *= 1.0 / engine_frame_time;

        if(0)
        {
            btScalar sub_tr[16];
            btTransform ang_tr;
            btVector3 mix_ang;
            Mat4_inv_Mat4_affine_mul(sub_tr, hair->owner_body_transform, new_transform);
            ang_tr.setFromOpenGLMatrix(sub_tr);
            ang_tr.getBasis().getEulerYPR(mix_ang.m_floats[2], mix_ang.m_floats[1], mix_ang.m_floats[0]);
            mix_ang *= 1.0 / engine_frame_time;

            // Looks like angular velocity breaks up constraints on VERY fast moves,
            // like mid-air turn. Probably, I've messed up with multiplier value...

            hair->elements[hair->root_index].body->setAngularVelocity(mix_ang);
            hair->owner_char->bt_body[hair->owner_body]->setAngularVelocity(mix_ang);
        }
        Mat4_Copy(hair->owner_body_transform, new_transform);*/

        // Set mixed velocities to both parent body and first hair body.

        //hair->elements[hair->root_index].body->setLinearVelocity(mix_vel);
        //hair->owner_char->bt_body[hair->owner_body]->setLinearVelocity(mix_vel);

        /*mix_vel *= -10.0;                                                     ///@FIXME: magick speed coefficient (force air hair friction!);
        for(int j=0;j<hair->element_count;j++)
        {
            hair->elements[j].body->applyCentralForce(mix_vel);
        }*/

        hair->container->room = physics->cont->room;
    }
}

struct hair_setup_s *Hair_GetSetup(struct lua_State *lua, uint32_t hair_entry_index)
{
    struct hair_setup_s *hair_setup = NULL;
    int top = lua_gettop(lua);

    lua_getglobal(lua, "getHairSetup");
    if(!lua_isfunction(lua, -1))
    {
        lua_settop(lua, top);
        return NULL;
    }

    lua_pushinteger(lua, hair_entry_index);
    if(!lua_CallAndLog(lua, 1, 1, 0) || !lua_istable(lua, -1))
    {
        lua_settop(lua, top);
        return NULL;
    }

    hair_setup = (struct hair_setup_s*)malloc(sizeof(struct hair_setup_s));
    hair_setup->model_id            = (uint32_t)lua_GetScalarField(lua, "model");
    hair_setup->link_body           = (uint32_t)lua_GetScalarField(lua, "link_body");
    hair_setup->vertex_map_count    = (uint32_t)lua_GetScalarField(lua, "v_count");

    lua_getfield(lua, -1, "props");
    if(!lua_istable(lua, -1))
    {
        free(hair_setup);
        lua_settop(lua, top);
        return NULL;
    }
    hair_setup->root_weight      = lua_GetScalarField(lua, "root_weight");
    hair_setup->tail_weight      = lua_GetScalarField(lua, "tail_weight");
    hair_setup->hair_inertia     = lua_GetScalarField(lua, "hair_inertia");
    hair_setup->hair_friction    = lua_GetScalarField(lua, "hair_friction");
    hair_setup->hair_restitution = lua_GetScalarField(lua, "hair_bouncing");
    hair_setup->joint_overlap    = lua_GetScalarField(lua, "joint_overlap");
    hair_setup->joint_cfm        = lua_GetScalarField(lua, "joint_cfm");
    hair_setup->joint_erp        = lua_GetScalarField(lua, "joint_erp");

    lua_getfield(lua, -1, "hair_damping");
    if(!lua_istable(lua, -1))
    {
        free(hair_setup);
        lua_settop(lua, top);
        return NULL;
    }
    hair_setup->hair_damping[0] = lua_GetScalarField(lua, 1);
    hair_setup->hair_damping[1] = lua_GetScalarField(lua, 2);

    lua_pop(lua, 1);
    lua_pop(lua, 1);

    lua_getfield(lua, -1, "v_index");
    if(!lua_istable(lua, -1))
    {
        free(hair_setup);
        lua_settop(lua, top);
        return NULL;
    }
    for(int i=1; i<=hair_setup->vertex_map_count; i++)
    {
        hair_setup->head_vertex_map[i-1] = (uint32_t)lua_GetScalarField(lua, i);
    }
    lua_pop(lua, 1);

    lua_getfield(lua, -1, "offset");
    if(!lua_istable(lua, -1))
    {
        free(hair_setup);
        lua_settop(lua, top);
        return NULL;
    }
    hair_setup->head_offset[0] = lua_GetScalarField(lua, 1);
    hair_setup->head_offset[1] = lua_GetScalarField(lua, 2);
    hair_setup->head_offset[2] = lua_GetScalarField(lua, 3);
    lua_pop(lua, 1);

    lua_getfield(lua, -1, "root_angle");
    if(!lua_istable(lua, -1))
    {
        free(hair_setup);
        lua_settop(lua, top);
        return NULL;
    }
    hair_setup->root_angle[0] = lua_GetScalarField(lua, 1);
    hair_setup->root_angle[1] = lua_GetScalarField(lua, 2);
    hair_setup->root_angle[2] = lua_GetScalarField(lua, 3);

    lua_settop(lua, top);
    return hair_setup;
}

int Hair_GetElementsCount(struct hair_s *hair)
{
    return (hair)?(hair->element_count):(0);
}

int Hair_GetElementInfo(struct hair_s *hair, int element, struct base_mesh_s **mesh, float tr[16])
{
#ifndef BT_USE_DOUBLE_PRECISION
    hair->elements[element].body->getWorldTransform().getOpenGLMatrix(tr);
#else
    btScalar btTr[16];
    hair->elements[element].body->getWorldTransform().getOpenGLMatrix(btTr);
    for(int i = 0; i < 16; i++)
    {
        tr[i] = btTr[i];
    }
#endif
    *mesh = hair->elements[element].mesh;
}


/* *****************************************************************************
 * ************************  RAGDOLL DATA  *************************************
 * ****************************************************************************/

// Joint setup struct is used to parse joint script entry to
// actual joint.

typedef struct rd_joint_setup_s
{
    uint16_t        body_index;     // Primary body index
    uint16_t        joint_type;     // See above as RD_CONSTRAINT_* definitions.

    float           body1_offset[3];   // Primary pivot point offset
    float           body2_offset[3];   // Secondary pivot point offset

    float           body1_angle[3]; // Primary pivot point angle
    float           body2_angle[3]; // Secondary pivot point angle

    float           joint_limit[3]; // Only first two are used for hinge constraint.

}rd_joint_setup_t, *rd_joint_setup_p;


// Ragdoll body setup is used to modify body properties for ragdoll needs.

typedef struct rd_body_setup_s
{
    float        mass;

    float        damping[2];
    float        restitution;
    float        friction;

}rd_body_setup_t, *rd_body_setup_p;


// Ragdoll setup struct is an unified structure which contains settings
// for ALL joints and bodies of a given ragdoll.

typedef struct rd_setup_s
{
    uint32_t            joint_count;
    uint32_t            body_count;

    float               joint_cfm;      // Constraint force mixing (joint softness)
    float               joint_erp;      // Error reduction parameter (joint "inertia")

    rd_joint_setup_s   *joint_setup;
    rd_body_setup_s    *body_setup;

    char               *hit_func;   // Later to be implemented as hit callback function.
}rd_setup_t, *rd_setup_p;


btScalar getInnerBBRadius(btScalar bb_min[3], btScalar bb_max[3])
{
    btScalar r = bb_max[0] - bb_min[0];
    btScalar t = bb_max[1] - bb_min[1];
    r = (t > r)?(r):(t);
    t = bb_max[2] - bb_min[2];
    return (t > r)?(r):(t);
}

bool Ragdoll_Create(struct physics_data_s *physics, struct ss_bone_frame_s *bf, struct rd_setup_s *setup)
{
    // No entity, setup or body count overflow - bypass function.

    if(!physics || !setup || (setup->body_count > physics->objects_count))
    {
        return false;
    }

    bool result = true;

    // If ragdoll already exists, overwrite it with new one.

    if(physics->bt_joint_count > 0)
    {
        result = Ragdoll_Delete(physics);
    }

    // Setup bodies.
    physics->bt_joint_count = 0;
    // update current character animation and full fix body to avoid starting ragdoll partially inside the wall or floor...
    /*Entity_UpdateCurrentBoneFrame(entity->bf, entity->transform);
    entity->no_fix_all = 0x00;
    entity->no_fix_skeletal_parts = 0x00000000;
    int map_size = entity->bf->animations.model->collision_map_size;             // does not works, strange...
    entity->bf->animations.model->collision_map_size = entity->bf->animations.model->mesh_count;
    Entity_FixPenetrations(entity, NULL);
    entity->bf->animations.model->collision_map_size = map_size;*/

    for(int i=0; i < setup->body_count; i++)
    {
        if(physics->bt_body[i] == NULL)
        {
            result = false;
            continue;   // If body is absent, return false and bypass this body setup.
        }

        btVector3 inertia (0.0, 0.0, 0.0);
        btScalar  mass = setup->body_setup[i].mass;

        bt_engine_dynamicsWorld->removeRigidBody(physics->bt_body[i]);

        physics->bt_body[i]->getCollisionShape()->calculateLocalInertia(mass, inertia);
        physics->bt_body[i]->setMassProps(mass, inertia);

        physics->bt_body[i]->updateInertiaTensor();
        physics->bt_body[i]->clearForces();

        physics->bt_body[i]->setLinearFactor (btVector3(1.0, 1.0, 1.0));
        physics->bt_body[i]->setAngularFactor(btVector3(1.0, 1.0, 1.0));

        physics->bt_body[i]->setDamping(setup->body_setup[i].damping[0], setup->body_setup[i].damping[1]);
        physics->bt_body[i]->setRestitution(setup->body_setup[i].restitution);
        physics->bt_body[i]->setFriction(setup->body_setup[i].friction);
        physics->bt_body[i]->setSleepingThresholds(RD_DEFAULT_SLEEPING_THRESHOLD, RD_DEFAULT_SLEEPING_THRESHOLD);

        if(bf->bone_tags[i].parent == NULL)
        {
            btScalar r = getInnerBBRadius(bf->bone_tags[i].mesh_base->bb_min, bf->bone_tags[i].mesh_base->bb_max);
            physics->bt_body[i]->setCcdMotionThreshold(0.8 * r);
            physics->bt_body[i]->setCcdSweptSphereRadius(r);
        }
    }

    for(uint16_t i = 0; i < setup->body_count; i++)
    {
        bt_engine_dynamicsWorld->addRigidBody(physics->bt_body[i]);
        physics->bt_body[i]->activate();
        physics->bt_body[i]->setLinearVelocity(btVector3(0.0, 0.0, 0.0));
        if(physics->ghostObjects[i])
        {
            bt_engine_dynamicsWorld->removeCollisionObject(physics->ghostObjects[i]);
            bt_engine_dynamicsWorld->addCollisionObject(physics->ghostObjects[i], COLLISION_NONE, COLLISION_NONE);
        }
    }

    // Setup constraints.
    physics->bt_joint_count = setup->joint_count;
    physics->bt_joints = (btTypedConstraint**)calloc(physics->bt_joint_count, sizeof(btTypedConstraint*));

    for(int i=0; i<physics->bt_joint_count; i++)
    {
        if( (setup->joint_setup[i].body_index >= setup->body_count) ||
            (physics->bt_body[setup->joint_setup[i].body_index] == NULL) )
        {
            result = false;
            break;       // If body 1 or body 2 are absent, return false and bypass this joint.
        }

        btTransform localA, localB;
        ss_bone_tag_p btB = bf->bone_tags + setup->joint_setup[i].body_index;
        ss_bone_tag_p btA = btB->parent;
        if(btA == NULL)
        {
            result = false;
            break;
        }
#if 0
        localA.setFromOpenGLMatrix(btB->transform);
        localB.setIdentity();
#else
        localA.getBasis().setEulerZYX(setup->joint_setup[i].body1_angle[0], setup->joint_setup[i].body1_angle[1], setup->joint_setup[i].body1_angle[2]);
        //localA.setOrigin(setup->joint_setup[i].body1_offset);
        localA.setOrigin(btVector3(btB->transform[12+0], btB->transform[12+1], btB->transform[12+2]));

        localB.getBasis().setEulerZYX(setup->joint_setup[i].body2_angle[0], setup->joint_setup[i].body2_angle[1], setup->joint_setup[i].body2_angle[2]);
        //localB.setOrigin(setup->joint_setup[i].body2_offset);
        localB.setOrigin(btVector3(0.0, 0.0, 0.0));
#endif

        switch(setup->joint_setup[i].joint_type)
        {
            case RD_CONSTRAINT_POINT:
                {
                    btPoint2PointConstraint* pointC = new btPoint2PointConstraint(*physics->bt_body[btA->index], *physics->bt_body[btB->index], localA.getOrigin(), localB.getOrigin());
                    physics->bt_joints[i] = pointC;
                }
                break;

            case RD_CONSTRAINT_HINGE:
                {
                    btHingeConstraint* hingeC = new btHingeConstraint(*physics->bt_body[btA->index], *physics->bt_body[btB->index], localA, localB);
                    hingeC->setLimit(setup->joint_setup[i].joint_limit[0], setup->joint_setup[i].joint_limit[1], 0.9, 0.3, 0.3);
                    physics->bt_joints[i] = hingeC;
                }
                break;

            case RD_CONSTRAINT_CONE:
                {
                    btConeTwistConstraint* coneC = new btConeTwistConstraint(*physics->bt_body[btA->index], *physics->bt_body[btB->index], localA, localB);
                    coneC->setLimit(setup->joint_setup[i].joint_limit[0], setup->joint_setup[i].joint_limit[1], setup->joint_setup[i].joint_limit[2], 0.9, 0.3, 0.7);
                    physics->bt_joints[i] = coneC;
                }
                break;
        }

        physics->bt_joints[i]->setParam(BT_CONSTRAINT_STOP_CFM, setup->joint_cfm, -1);
        physics->bt_joints[i]->setParam(BT_CONSTRAINT_STOP_ERP, setup->joint_erp, -1);

        physics->bt_joints[i]->setDbgDrawSize(64.0);
        bt_engine_dynamicsWorld->addConstraint(physics->bt_joints[i], true);
    }

    if(result == false)
    {
        Ragdoll_Delete(physics);  // PARANOID: Clean up the mess, if something went wrong.
    }
    return result;
}


bool Ragdoll_Delete(struct physics_data_s *physics)
{
    if(physics->bt_joint_count == 0)
    {
        return false;
    }

    for(int i=0; i<physics->bt_joint_count; i++)
    {
        if(physics->bt_joints[i])
        {
            bt_engine_dynamicsWorld->removeConstraint(physics->bt_joints[i]);
            delete physics->bt_joints[i];
            physics->bt_joints[i] = NULL;
        }
    }

    for(int i=0; i < physics->objects_count; i++)
    {
        bt_engine_dynamicsWorld->removeRigidBody(physics->bt_body[i]);
        physics->bt_body[i]->setMassProps(0, btVector3(0.0, 0.0, 0.0));
        bt_engine_dynamicsWorld->addRigidBody(physics->bt_body[i], COLLISION_GROUP_KINEMATIC, COLLISION_MASK_ALL);
        if(physics->ghostObjects[i])
        {
            bt_engine_dynamicsWorld->removeCollisionObject(physics->ghostObjects[i]);
            bt_engine_dynamicsWorld->addCollisionObject(physics->ghostObjects[i], COLLISION_GROUP_CHARACTERS, COLLISION_MASK_ALL);
        }
    }

    free(physics->bt_joints);
    physics->bt_joints = NULL;
    physics->bt_joint_count = 0;

    return true;

    // NB! Bodies remain in the same state!
    // To make them static again, additionally call setEntityBodyMass script function.
}


struct rd_setup_s *Ragdoll_GetSetup(struct lua_State *lua, int ragdoll_index)
{
    struct rd_setup_s *setup = NULL;
    int top = lua_gettop(lua);

    lua_getglobal(lua, "getRagdollSetup");
    if(!lua_isfunction(lua, -1))
    {
        lua_settop(lua, top);
        return NULL;
    }

    lua_pushinteger(lua, ragdoll_index);
    if(!lua_CallAndLog(lua, 1, 1, 0) || !lua_istable(lua, -1))
    {
        lua_settop(lua, top);
        return NULL;
    }

    lua_getfield(lua, -1, "hit_callback");
    if(!lua_isstring(lua, -1))
    {
        lua_settop(lua, top);
        return NULL;
    }

    setup = (rd_setup_p)malloc(sizeof(rd_setup_t));
    setup->body_count = 0;
    setup->joint_count = 0;
    setup->body_setup = NULL;
    setup->joint_setup = NULL;
    setup->hit_func = NULL;
    setup->joint_cfm = 0.0;
    setup->joint_erp = 0.0;

    size_t string_length = 0;
    const char* func_name = lua_tolstring(lua, -1, &string_length);
    setup->hit_func = (char*)calloc(string_length, sizeof(char));
    memcpy(setup->hit_func, func_name, string_length * sizeof(char));
    lua_pop(lua, 1);

    setup->joint_count = (uint32_t)lua_GetScalarField(lua, "joint_count");
    setup->body_count  = (uint32_t)lua_GetScalarField(lua, "body_count");

    setup->joint_cfm   = lua_GetScalarField(lua, "joint_cfm");
    setup->joint_erp   = lua_GetScalarField(lua, "joint_erp");

    if((setup->body_count <= 0) || (setup->joint_count <= 0))
    {
        Ragdoll_DeleteSetup(setup);
        setup = NULL;
        lua_settop(lua, top);
        return NULL;
    }

    lua_getfield(lua, -1, "body");
    if(!lua_istable(lua, -1))
    {
        Ragdoll_DeleteSetup(setup);
        setup = NULL;
        lua_settop(lua, top);
        return NULL;
    }

    setup->body_setup  = (rd_body_setup_p)calloc(setup->body_count, sizeof(rd_body_setup_t));
    setup->joint_setup = (rd_joint_setup_p)calloc(setup->joint_count, sizeof(rd_joint_setup_t));

    for(int i=0; i<setup->body_count; i++)
    {
        lua_rawgeti(lua, -1, i+1);
        if(!lua_istable(lua, -1))
        {
            Ragdoll_DeleteSetup(setup);
            setup = NULL;
            lua_settop(lua, top);
            return NULL;
        }
        setup->body_setup[i].mass = lua_GetScalarField(lua, "mass");
        setup->body_setup[i].restitution = lua_GetScalarField(lua, "restitution");
        setup->body_setup[i].friction = lua_GetScalarField(lua, "friction");

        lua_getfield(lua, -1, "damping");
        if(!lua_istable(lua, -1))
        {
            Ragdoll_DeleteSetup(setup);
            setup = NULL;
            lua_settop(lua, top);
            return NULL;
        }

        setup->body_setup[i].damping[0] = lua_GetScalarField(lua, 1);
        setup->body_setup[i].damping[1] = lua_GetScalarField(lua, 2);
        lua_pop(lua, 1);
        lua_pop(lua, 1);
    }
    lua_pop(lua, 1);

    lua_getfield(lua, -1, "joint");
    if(!lua_istable(lua, -1))
    {
        Ragdoll_DeleteSetup(setup);
        setup = NULL;
        lua_settop(lua, top);
        return NULL;
    }

    for(int i=0; i<setup->joint_count; i++)
    {
        lua_rawgeti(lua, -1, i+1);
        if(!lua_istable(lua, -1))
        {
            Ragdoll_DeleteSetup(setup);
            setup = NULL;
            lua_settop(lua, top);
            return NULL;
        }
        setup->joint_setup[i].body_index = (uint16_t)lua_GetScalarField(lua, "body_index");
        setup->joint_setup[i].joint_type = (uint16_t)lua_GetScalarField(lua, "joint_type");

        lua_getfield(lua, -1, "body1_offset");
        if(!lua_istable(lua, -1))
        {
            Ragdoll_DeleteSetup(setup);
            setup = NULL;
            lua_settop(lua, top);
            return NULL;
        }
        setup->joint_setup[i].body1_offset[0] = lua_GetScalarField(lua, 1);
        setup->joint_setup[i].body1_offset[1] = lua_GetScalarField(lua, 2);
        setup->joint_setup[i].body1_offset[2] = lua_GetScalarField(lua, 3);
        lua_pop(lua, 1);

        lua_getfield(lua, -1, "body2_offset");
        if(!lua_istable(lua, -1))
        {
            Ragdoll_DeleteSetup(setup);
            setup = NULL;
            lua_settop(lua, top);
            return NULL;
        }
        setup->joint_setup[i].body2_offset[0] = lua_GetScalarField(lua, 1);
        setup->joint_setup[i].body2_offset[1] = lua_GetScalarField(lua, 2);
        setup->joint_setup[i].body2_offset[2] = lua_GetScalarField(lua, 3);
        lua_pop(lua, 1);

        lua_getfield(lua, -1, "body1_angle");
        if(!lua_istable(lua, -1))
        {
            Ragdoll_DeleteSetup(setup);
            setup = NULL;
            lua_settop(lua, top);
            return NULL;
        }
        setup->joint_setup[i].body1_angle[0] = lua_GetScalarField(lua, 1);
        setup->joint_setup[i].body1_angle[1] = lua_GetScalarField(lua, 2);
        setup->joint_setup[i].body1_angle[2] = lua_GetScalarField(lua, 3);
        lua_pop(lua, 1);

        lua_getfield(lua, -1, "body2_angle");
        if(!lua_istable(lua, -1))
        {
            Ragdoll_DeleteSetup(setup);
            setup = NULL;
            lua_settop(lua, top);
            return NULL;
        }
        setup->joint_setup[i].body2_angle[0] = lua_GetScalarField(lua, 1);
        setup->joint_setup[i].body2_angle[1] = lua_GetScalarField(lua, 2);
        setup->joint_setup[i].body2_angle[2] = lua_GetScalarField(lua, 3);
        lua_pop(lua, 1);

        lua_getfield(lua, -1, "joint_limit");
        if(!lua_istable(lua, -1))
        {
            Ragdoll_DeleteSetup(setup);
            setup = NULL;
            lua_settop(lua, top);
            return NULL;
        }
        setup->joint_setup[i].joint_limit[0] = lua_GetScalarField(lua, 1);
        setup->joint_setup[i].joint_limit[1] = lua_GetScalarField(lua, 2);
        setup->joint_setup[i].joint_limit[2] = lua_GetScalarField(lua, 3);
        lua_pop(lua, 1);
        lua_pop(lua, 1);
    }

    lua_settop(lua, top);
    return setup;
}


void Ragdoll_DeleteSetup(struct rd_setup_s *setup)
{
    if(setup)
    {
        free(setup->body_setup);
        setup->body_setup = NULL;
        setup->body_count = 0;

        free(setup->joint_setup);
        setup->joint_setup = NULL;
        setup->joint_count = 0;

        free(setup->hit_func);
        setup->hit_func = NULL;

        free(setup);
    }
}