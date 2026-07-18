#include "ColliderGJK.h"
#include "ColliderAnalytical.h"
#include "../ecs/Registry.h"
#include "../ecs/Components.h"
#include "../world/Transform.h"
#include <cmath>
#include <algorithm>

namespace physics {

namespace ColliderDiscrete {

//both the simplex and the search direction need to be updated so pass those both in by reference
bool updateSimplex(GJKSimplex& simplex, math::Vec3& direction) {
    //note: points are described in reverse addition order
    //further down the alphabet means older
    //so b got added before a, after c
    switch(simplex.n) {
        case 2: {
            //we have 2 points, oldest one in b.
            //we need to determine which region the origin is in
            //since a was in the direction of the origin from 0, this simplifies hte check
            //we only need to compare whether the ray ab is in the direction of the origin or not

            //"in the direction of the origin" <==> ab * (ao <==> -a) >0
            math::Vec3 ao = -simplex.points[1];
            math::Vec3 ab = simplex.points[0] - simplex.points[1];

            if(ab.dot(ao) > 0) {
                //origin contained witihn the 2 planes defined by a and b
                //simplex already has a & b so only thing left is dircetion computation
                //here use triple cross product (ab x ao x ab) to get a vector that is:
                // 1 perpendicular to ab (we don't want to search in a redundant direction, ab already covered)
                // 2 in the plane of ab and ao
                math::Vec3 aCrossb = ab.cross(ao);
                if(aCrossb.dot(aCrossb) < GJK_EPS) {

                    //we have colinear points
                    //check if the origin is inbetween
                    if(simplex.points[0].dot(simplex.points[1]) <= 0) {
                        //origin in between
                        return true;
                    }else {
                        //origin past a (update simplex to only have a)
                        simplex.points[0] = simplex.points[1]; //zeroing simplex.points[1] not necessary as any pushes will ovewrite it
                        simplex.n = 1;
                        // (just n needs to be updated to reflect the true length)

                        //direction is just ao
                        direction = ao;
                        return false;
                    }
                }
                direction = (aCrossb).cross(ab);
                return false;
            }else {
                //origin past a (update simplex to only have a)
                simplex.points[0] = simplex.points[1]; //zeroing simplex.points[1] not necessary as any pushes will ovewrite it
                simplex.n = 1;
                // (just n needs to be updated to reflect the true length)

                //direction is just ao
                direction = ao;
                return false;
            }
        }
        case 3: {
            //first compute triangle normal as ab cross ac
            math::Vec3 ao = -simplex.points[2];
            math::Vec3 ab = simplex.points[1] - simplex.points[2];
            math::Vec3 ac = simplex.points[0] - simplex.points[2];

            math::Vec3 triangleNormal = ab.cross(ac);

            //ab line already tested, so it has to be past there
            //so we test one of the other edges, abc x ac or ab x abc

            math::Vec3 edge1 = triangleNormal.cross(ac);

            if(edge1.dot(ao) > 0) {
                //past the edge of the triangle, need to determine if its past a or not
                if(ac.dot(ao) > 0) {
                    //it is, simplex is ac
                    simplex.points[1] = simplex.points[2]; //zeroing simplex.points[2] not necessary as any pushes will ovewrite it
                    simplex.n = 2;

                    //search direction is the same as the line case for 2 points (ac x ao x ac)
                    direction = (ac.cross(ao)).cross(ac);
                    return false;
                }else {
                    //behind ac, must test if behind b as well (some weird triangles can cause this)
                    if(ab.dot(ao) > 0) {
                        //within the region bound by a and b (simplex is a and b)

                        simplex.points[0] = simplex.points[1];
                        simplex.points[1] = simplex.points[2];
                        simplex.n--; //zeroing simplex.points[2] not necessary as any pushes will ovewrite it

                        //do usual line calculation
                        direction = (ab.cross(ao)).cross(ab);
                        return false;
                    }else {
                        //behind a for sure (tested both edges)
                        //simplex is simply a now
                        simplex.points[0] = simplex.points[2];
                        simplex.n = 1; //zeroing simplex.points[1/2] not necessary as any pushes will overwrite it

                        direction = ao;
                        return false;
                    }
                }
            }else {
                math::Vec3 edge2 = ab.cross(triangleNormal);
                //now test to see if beyond the other edge
                if(edge2.dot(ao) > 0) {
                    //we are indeed beyond the other edge, now test beyond/behind ab (same case as else above)
                    if(ab.dot(ao) > 0) {
                        //within the region bound by a and b (simplex is a and b)

                        simplex.points[0] = simplex.points[1];
                        simplex.points[1] = simplex.points[2];
                        simplex.n--; //zeroing simplex.points[2] not necessary as any pushes will ovewrite it

                        //do usual line calculation
                        direction = (ab.cross(ao)).cross(ab);
                        return false;
                    }else {
                        //behind a for sure (tested both edges)
                        //simplex is simply a now
                        simplex.points[0] = simplex.points[2];
                        simplex.n = 1; //zeroing simplex.points[1/2] not necessary as any pushes will overwrite it

                        direction = ao;
                        return false;
                    }
                }else {
                    //within both edges, origin must be above or below triangle
                    //test via triangle normal now
                    if(triangleNormal.dot(ao) > 0) {
                        //above the triangle, winding is correct
                        //update direction with new search direction
                        direction = triangleNormal;
                        return false;
                    }else {
                        //reverse winding (swap b and c)
                        math::Vec3 temp = simplex.points[0];
                        simplex.points[0] = simplex.points[1];
                        simplex.points[1] = temp;

                        //flip the direction since the origin is below
                        direction = -triangleNormal;
                        return false;
                    }
                }
            }
            break;
        }
        case 4: {
            //treat this as a series of triangle tests
            //in general, a tetrahedron divides 3d space into 15 regions (one interior, 4 faces, 6 edges, 4 points)
            //however, results can be reused since we know the triangle build order and the tests that were used to find the points
            //ex: testing the triangle normal of dcb is redundant since that is how point a got added

            //always compare with ao so compute here
            math::Vec3 ao = -simplex.points[3];

            //the one with the least apriori info is triangle abc so start with that test
            math::Vec3 ab = simplex.points[2] - simplex.points[3];
            math::Vec3 ac = simplex.points[1] - simplex.points[3];

            //find by crossing (note we always want normals to point into the tetrahedron
            // so we cross in the order that produces the vector towards the missing vertex
            // ex: (acb -> ac x ab poitns towards d)))
            //this means ABOVE a plane (in this tetrahedral case) means into the interior and vice versa
            math::Vec3 abcNormal = ac.cross(ab);

            if(abcNormal.dot(ao) > 0) {
                //inside/above the abc plane

                //now test abd plane (plane with 2nd least info)
                math::Vec3 ad = simplex.points[0] - simplex.points[3];

                //follow inward normal convention
                math::Vec3 abdNormal = ab.cross(ad);

                if(abdNormal.dot(ao) > 0) {
                    //inside/above abd plane

                    //now test against acd plane (plane with the most info)
                    math::Vec3 acdNormal = ad.cross(ac);
                    if(acdNormal.dot(ao) > 0) {
                        //within all 3 planes woo hoo
                        return false;
                    }else {
                        //outside this plane, check both edges
                        math::Vec3 edge1 = acdNormal.cross(ac);
                        if(edge1.dot(ao) > 0) {
                            //beyond this edge, check if behind a in the dir of c
                            if(ac.dot(ao) > 0) {
                                //c check redundant so
                                //in between a and c
                                simplex.points[0] = simplex.points[1];
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //do line simplex dir calc
                                direction = (ac.cross(ao)).cross(ac);
                                return false;
                            }else {
                                //behind a in the dir of c, now check if behind a in the dir of d
                                if(ad.dot(ao) > 0) {
                                    //d check redundant so
                                    //in between a and d
                                    simplex.points[1] = simplex.points[3];
                                    simplex.n = 2;

                                    //do line simplex dir calc
                                    direction = (ad.cross(ao)).cross(ad);
                                    return false;
                                }else {
                                    //behind a in both dirs, so only point is a
                                    simplex.points[0] = simplex.points[3];
                                    simplex.n = 1;

                                    direction = ao;
                                    return false;
                                }
                            }
                        }else {
                            //within this edge, now chec kif beyond the other edge
                            math::Vec3 edge2 = ad.cross(acdNormal);
                            if(edge2.dot(ao) > 0) {
                                //beyond this edge too, check if behind a in the dir of d
                                //behind a in the dir of c, now check if behind a in the dir of d
                                if(ad.dot(ao) > 0) {
                                    //d check redundant so
                                    //in between a and d
                                    simplex.points[1] = simplex.points[3];
                                    simplex.n = 2;

                                    //do line simplex dir calc
                                    direction = (ad.cross(ao)).cross(ad);
                                    return false;
                                }else {
                                    //behind a in both dirs, so only point is a
                                    simplex.points[0] = simplex.points[3];
                                    simplex.n = 1;

                                    direction = ao;
                                    return false;
                                }
                            }else {
                                //within both edges and we checked the plane already so outside
                                //reverse c and d
                                math::Vec3 temp = simplex.points[0];
                                simplex.points[0] = simplex.points[1];
                                simplex.points[1] = temp;
                                simplex.points[2] = simplex.points[3]; //put a in third place
                                simplex.n = 3;

                                //dir is -acd
                                direction = -acdNormal;
                                return false;
                            }
                        }
                    }
                }else {
                    //below/outside of abd plane

                    //test first against an edge
                    math::Vec3 edge1 = ab.cross(abdNormal);

                    if(edge1.dot(ao) > 0) {
                        //past this edge
                        //check ab to see if its behind a in the dir of b
                        if(ab.dot(ao) > 0) {
                            //also check b since we have NEVER checked behind the third point ever in the building of the simplex
                            //same sign optimization (flip check sign to avoid computing negative of vector)
                            if(simplex.points[2].dot(ao) < 0) {
                                //closest 2 points are ab
                                simplex.points[0] = simplex.points[2];
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //do same direction calc
                                direction = (ab.cross(ao)).cross(ab);
                                return false;
                            }else {
                                //closest point is b only
                                simplex.points[0] = simplex.points[2];
                                simplex.n = 1;

                                //direction is simply towards the origin
                                direction = -simplex.points[0];
                                return false;
                            }
                        }else {
                            //behind a in the direction of b, now check if behind in the direction of d
                            if(ad.dot(ao) > 0) {
                                //d check already done (literally the first step in buliding the simplex)
                                //so only 2 points are a and d
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //same line simplex direction computation
                                direction = (ad.cross(ao)).cross(ad);
                                return false;
                            }else {
                                //checked behind both edges, only point is a
                                simplex.points[0] = simplex.points[3];
                                simplex.n = 1;

                                //just go toward the origin
                                direction = ao;
                                return false;
                            }
                        }
                    }else {
                        //within this edge, check the other edge
                        math::Vec3 edge2 = abdNormal.cross(ad);

                        if(edge2.dot(ao) > 0) {
                            //beyond this edge as well, check if within ad (d check not needed)
                            //behind a in the direction of b, now check in the direction of d
                            if(ad.dot(ao) > 0) {
                                //d check already done (literally the first step in buliding the simplex)
                                //so only 2 points are a and d
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //same line simplex direction computation
                                direction = (ad.cross(ao)).cross(ad);
                                return false;
                            }else {
                                //checked behind both edges, only point is a
                                simplex.points[0] = simplex.points[3];
                                simplex.n = 1;

                                //just go toward the origin
                                direction = ao;
                                return false;
                            }
                        }else {
                            //within this edge as well, and we tested against the plane already so it must be pointing outward
                            //reverse orientation
                            simplex.points[1] = simplex.points[2]; //b goes to c's spot
                            simplex.points[2] = simplex.points[3]; //a stays a
                            simplex.n = 3;

                            //direction is -abd
                            direction = -abdNormal;
                            return false;
                        }
                    }
                }
            }else {
                //below/outside abc plane
                //due to the lack of tests on the third point (b), some more tests need to be added post c and b (those voronoi regions haven't been tested)

                //first test against edge (good thing we already computed for acb normal)
                math::Vec3 edge1 = ac.cross(abcNormal);
                if(edge1.dot(ao) > 0) {
                    //past this edge
                    //test if within ac or not
                    //note: do NOT need to check past c because that test is redudant from past tests/simplex building algo
                    if(ac.dot(ao) > 0) {
                        //within the edge, simplex is ac only
                        simplex.points[0] = simplex.points[1];
                        simplex.points[1] = simplex.points[3];
                        simplex.n = 2; //zeroing not necessary, just set n = 2 (overwrites)

                        //direction is same as line case for simplex
                        direction = ac.cross(ao).cross(ac);
                        return false;
                    }else {
                        //check ab to see if its behind a in the dir of b
                        if(ab.dot(ao) > 0) {
                            //also check b since we have NEVER checked behind the third point ever in the building of the simplex
                            //same sign optimization (flip check sign to avoid computing negative of number)
                            if(simplex.points[2].dot(ao) < 0) {
                                //closest 2 points are ab
                                simplex.points[0] = simplex.points[2];
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //do same direction calc
                                direction = (ab.cross(ao)).cross(ab);
                                return false;
                            }else {
                                //closest point is b only
                                simplex.points[0] = simplex.points[2];
                                simplex.n = 1;

                                //direction is simply towards the origin
                                direction = -simplex.points[0];
                                return false;
                            }
                        }else {
                            //its behind a on both ab and ac directions, so a only
                            simplex.points[0] = simplex.points[3];
                            simplex.n = 1;

                            //direction is simply towards the origin
                            direction = -simplex.points[0];

                            return false;
                        }
                    }
                }else {
                    //within the edge
                    //test the other edge to see if within the triangle
                    math::Vec3 edge2 = abcNormal.cross(ab);

                    if(edge2.dot(ao) > 0) {
                        //beyond this edge, do the same ab checks
                        //check ab to see if its behind a in the dir of b
                        if(ab.dot(ao) > 0) {
                            //also check b since we have NEVER checked behind the third point ever in the building of the simplex
                            //same sign optimization (flip check sign to avoid computing negative of number)
                            if(simplex.points[2].dot(ao) < 0) {
                                //closest 2 points are ab
                                simplex.points[0] = simplex.points[2];
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //do same direction calc
                                direction = (ab.cross(ao)).cross(ab);
                                return false;
                            }else {
                                //closest point is b only
                                simplex.points[0] = simplex.points[2];
                                simplex.n = 1;

                                //direction is simply towards the origin
                                direction = -simplex.points[0];
                                return false;
                            }
                        }else {
                            //its behind a on both ab and ac directions, so a only
                            simplex.points[0] = simplex.points[3];
                            simplex.n = 1;

                            //direction is simply towards the origin
                            direction = -simplex.points[0];
                            return false;
                        }
                    }else {
                        //within both edges, and the very first check we did was plane normal
                        //we are outside/below the plane
                        //so winding is incorrect
                        simplex.points[0] = simplex.points[2]; //b becomes oldest
                        //c remains in place (swap functions this way)
                        simplex.points[2] = simplex.points[3]; //a becomes the newest
                        simplex.n = 3;

                        //direciton is the negative (since we need to look the other way
                        direction = -abcNormal;
                        return false;
                    }
                }
            }
            break;
        }
    }

    return false;
}

/*

//both the simplex and the search direction need to be updated so pass those both in by reference
bool updateSimplex(GJKSimplex& simplex, math::Vec3& direction) {
    //note: points are described in reverse addition order
    //further down the alphabet means older
    //so b got added before a, after c
    switch(simplex.n) {
        case 2: {
            //we have 2 points, oldest one in b.
            //we need to determine which region the origin is in
            //since a was in the direction of the origin from 0, this simplifies hte check
            //we only need to compare whether the ray ab is in the direction of the origin or not

            //"in the direction of the origin" <==> ab * (ao <==> -a) >0
            math::Vec3 ao = -simplex.points[1];
            math::Vec3 ab = simplex.points[0] - simplex.points[1];

            if(ab.dot(ao) > 0) {
                //origin contained witihn the 2 planes defined by a and b
                //simplex already has a & b so only thing left is dircetion computation
                //here use triple cross product (ab x ao x ab) to get a vector that is:
                // 1 perpendicular to ab (we don't want to search in a redundant direction, ab already covered)
                // 2 in the plane of ab and ao
                math::Vec3 aCrossb = ab.cross(ao);
                if(aCrossb.dot(aCrossb) < GJK_EPS) {

                    //we have colinear points
                    //check if the origin is inbetween
                    if(simplex.points[0].dot(simplex.points[1]) <= 0) {
                        //origin in between
                        return true;
                    }else {
                        //origin past a (update simplex to only have a)
                        simplex.points[0] = simplex.points[1]; //zeroing simplex.points[1] not necessary as any pushes will ovewrite it
                        simplex.n = 1;
                        // (just n needs to be updated to reflect the true length)

                        //direction is just ao
                        direction = ao;
                        return false;
                    }
                }
                direction = (aCrossb).cross(ab);
                return false;
            }else {
                //origin past a (update simplex to only have a)
                simplex.points[0] = simplex.points[1]; //zeroing simplex.points[1] not necessary as any pushes will ovewrite it
                simplex.n = 1;
                // (just n needs to be updated to reflect the true length)

                //direction is just ao
                direction = ao;
                return false;
            }
        }
        case 3: {
            //first compute triangle normal as ab cross ac
            math::Vec3 ao = -simplex.points[2];
            math::Vec3 ab = simplex.points[1] - simplex.points[2];
            math::Vec3 ac = simplex.points[0] - simplex.points[2];

            math::Vec3 triangleNormal = ab.cross(ac);

            //ab line already tested, so it has to be past there
            //so we test one of the other edges, abc x ac or ab x abc

            math::Vec3 edge1 = triangleNormal.cross(ac);

            if(edge1.dot(ao) > 0) {
                //past the edge of the triangle, need to determine if its past a or not
                if(ac.dot(ao) > 0) {
                    //it is, simplex is ac
                    simplex.points[1] = simplex.points[2]; //zeroing simplex.points[2] not necessary as any pushes will ovewrite it
                    simplex.n = 2;

                    //search direction is the same as the line case for 2 points (ac x ao x ac)
                    direction = (ac.cross(ao)).cross(ac);
                    return false;
                }else {
                    //behind ac, must test if behind b as well (some weird triangles can cause this)
                    if(ab.dot(ao) > 0) {
                        //within the region bound by a and b (simplex is a and b)

                        simplex.points[0] = simplex.points[1];
                        simplex.points[1] = simplex.points[2];
                        simplex.n--; //zeroing simplex.points[2] not necessary as any pushes will ovewrite it

                        //do usual line calculation
                        direction = (ab.cross(ao)).cross(ab);
                        return false;
                    }else {
                        //behind a for sure (tested both edges)
                        //simplex is simply a now
                        simplex.points[0] = simplex.points[2];
                        simplex.n = 1; //zeroing simplex.points[1/2] not necessary as any pushes will overwrite it

                        direction = ao;
                        return false;
                    }
                }
            }else {
                math::Vec3 edge2 = ab.cross(triangleNormal);
                //now test to see if beyond the other edge
                if(edge2.dot(ao) > 0) {
                    //we are indeed beyond the other edge, now test beyond/behind ab (same case as else above)
                    if(ab.dot(ao) > 0) {
                        //within the region bound by a and b (simplex is a and b)

                        simplex.points[0] = simplex.points[1];
                        simplex.points[1] = simplex.points[2];
                        simplex.n--; //zeroing simplex.points[2] not necessary as any pushes will ovewrite it

                        //do usual line calculation
                        direction = (ab.cross(ao)).cross(ab);
                        return false;
                    }else {
                        //behind a for sure (tested both edges)
                        //simplex is simply a now
                        simplex.points[0] = simplex.points[2];
                        simplex.n = 1; //zeroing simplex.points[1/2] not necessary as any pushes will overwrite it

                        direction = ao;
                        return false;
                    }
                }else {
                    //within both edges, origin must be above or below triangle
                    //test via triangle normal now
                    if(triangleNormal.dot(ao) > 0) {
                        //above the triangle, winding is correct
                        //update direction with new search direction
                        direction = triangleNormal;
                        return false;
                    }else {
                        //reverse winding (swap b and c)
                        math::Vec3 temp = simplex.points[0];
                        simplex.points[0] = simplex.points[1];
                        simplex.points[1] = temp;

                        //flip the direction since the origin is below
                        direction = -triangleNormal;
                        return false;
                    }
                }
            }
            break;
        }
        case 4: {
            //treat this as a series of triangle tests
            //in general, a tetrahedron divides 3d space into 15 regions (one interior, 4 faces, 6 edges, 4 points)
            //however, results can be reused since we know the triangle build order and the tests that were used to find the points
            //ex: testing the triangle normal of dcb is redundant since that is how point a got added

            //always compare with ao so compute here
            math::Vec3 ao = -simplex.points[3];

            //the one with the least apriori info is triangle abc so start with that test
            math::Vec3 ab = simplex.points[2] - simplex.points[3];
            math::Vec3 ac = simplex.points[1] - simplex.points[3];

            //find by crossing (note we always want normals to point into the tetrahedron
            // so we cross in the order that produces the vector towards the missing vertex
            // ex: (acb -> ac x ab poitns towards d)))
            //this means ABOVE a plane (in this tetrahedral case) means into the interior and vice versa
            math::Vec3 abcNormal = ac.cross(ab);

            if(abcNormal.dot(ao) > 0) {
                //inside/above the abc plane

                //now test abd plane (plane with 2nd least info)
                math::Vec3 ad = simplex.points[0] - simplex.points[3];

                //follow inward normal convention
                math::Vec3 abdNormal = ab.cross(ad);

                if(abdNormal.dot(ao) > 0) {
                    //inside/above abd plane

                    //now test against acd plane (plane with the most info)
                    math::Vec3 acdNormal = ad.cross(ac);
                    if(acdNormal.dot(ao) > 0) {
                        //within all 3 planes woo hoo
                        return false;
                    }else {
                        //outside this plane, check both edges
                        math::Vec3 edge1 = acdNormal.cross(ac);
                        if(edge1.dot(ao) > 0) {
                            //beyond this edge, check if behind a in the dir of c
                            if(ac.dot(ao) > 0) {
                                //c check redundant so
                                //in between a and c
                                simplex.points[0] = simplex.points[1];
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //do line simplex dir calc
                                direction = (ac.cross(ao)).cross(ac);
                                return false;
                            }else {
                                //behind a in the dir of c, now check if behind a in the dir of d
                                if(ad.dot(ao) > 0) {
                                    //d check redundant so
                                    //in between a and d
                                    simplex.points[1] = simplex.points[3];
                                    simplex.n = 2;

                                    //do line simplex dir calc
                                    direction = (ad.cross(ao)).cross(ad);
                                    return false;
                                }else {
                                    //behind a in both dirs, so only point is a
                                    simplex.points[0] = simplex.points[3];
                                    simplex.n = 1;

                                    direction = ao;
                                    return false;
                                }
                            }
                        }else {
                            //within this edge, now chec kif beyond the other edge
                            math::Vec3 edge2 = ad.cross(acdNormal);
                            if(edge2.dot(ao) > 0) {
                                //beyond this edge too, check if behind a in the dir of d
                                //behind a in the dir of c, now check if behind a in the dir of d
                                if(ad.dot(ao) > 0) {
                                    //d check redundant so
                                    //in between a and d
                                    simplex.points[1] = simplex.points[3];
                                    simplex.n = 2;

                                    //do line simplex dir calc
                                    direction = (ad.cross(ao)).cross(ad);
                                    return false;
                                }else {
                                    //behind a in both dirs, so only point is a
                                    simplex.points[0] = simplex.points[3];
                                    simplex.n = 1;

                                    direction = ao;
                                    return false;
                                }
                            }else {
                                //within both edges and we checked the plane already so outside
                                //reverse c and d
                                math::Vec3 temp = simplex.points[0];
                                simplex.points[0] = simplex.points[1];
                                simplex.points[1] = temp;
                                simplex.points[2] = simplex.points[3]; //put a in third place
                                simplex.n = 3;

                                //dir is -acd
                                direction = -acdNormal;
                                return false;
                            }
                        }
                    }
                }else {
                    //below/outside of abd plane

                    //test first against an edge
                    math::Vec3 edge1 = ab.cross(abdNormal);

                    if(edge1.dot(ao) > 0) {
                        //past this edge
                        //check ab to see if its behind a in the dir of b
                        if(ab.dot(ao) > 0) {
                            //also check b since we have NEVER checked behind the third point ever in the building of the simplex
                            //same sign optimization (flip check sign to avoid computing negative of vector)
                            if(simplex.points[2].dot(ao) < 0) {
                                //closest 2 points are ab
                                simplex.points[0] = simplex.points[2];
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //do same direction calc
                                direction = (ab.cross(ao)).cross(ab);
                                return false;
                            }else {
                                //closest point is b only
                                simplex.points[0] = simplex.points[2];
                                simplex.n = 1;

                                //direction is simply towards the origin
                                direction = -simplex.points[0];
                                return false;
                            }
                        }else {
                            //behind a in the direction of b, now check if behind in the direction of d
                            if(ad.dot(ao) > 0) {
                                //d check already done (literally the first step in buliding the simplex)
                                //so only 2 points are a and d
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //same line simplex direction computation
                                direction = (ad.cross(ao)).cross(ad);
                                return false;
                            }else {
                                //checked behind both edges, only point is a
                                simplex.points[0] = simplex.points[3];
                                simplex.n = 1;

                                //just go toward the origin
                                direction = ao;
                                return false;
                            }
                        }
                    }else {
                        //within this edge, check the other edge
                        math::Vec3 edge2 = abdNormal.cross(ad);

                        if(edge2.dot(ao) > 0) {
                            //beyond this edge as well, check if within ad (d check not needed)
                            //behind a in the direction of b, now check in the direction of d
                            if(ad.dot(ao) > 0) {
                                //d check already done (literally the first step in buliding the simplex)
                                //so only 2 points are a and d
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //same line simplex direction computation
                                direction = (ad.cross(ao)).cross(ad);
                                return false;
                            }else {
                                //checked behind both edges, only point is a
                                simplex.points[0] = simplex.points[3];
                                simplex.n = 1;

                                //just go toward the origin
                                direction = ao;
                                return false;
                            }
                        }else {
                            //within this edge as well, and we tested against the plane already so it must be pointing outward
                            //reverse orientation
                            simplex.points[1] = simplex.points[2]; //b goes to c's spot
                            simplex.points[2] = simplex.points[3]; //a stays a
                            simplex.n = 3;

                            //direction is -abd
                            direction = -abdNormal;
                            return false;
                        }
                    }
                }
            }else {
                //below/outside abc plane
                //due to the lack of tests on the third point (b), some more tests need to be added post c and b (those voronoi regions haven't been tested)

                //first test against edge (good thing we already computed for acb normal)
                math::Vec3 edge1 = ac.cross(abcNormal);
                if(edge1.dot(ao) > 0) {
                    //past this edge
                    //test if within ac or not
                    //note: do NOT need to check past c because that test is redudant from past tests/simplex building algo
                    if(ac.dot(ao) > 0) {
                        //within the edge, simplex is ac only
                        simplex.points[0] = simplex.points[1];
                        simplex.points[1] = simplex.points[3];
                        simplex.n = 2; //zeroing not necessary, just set n = 2 (overwrites)

                        //direction is same as line case for simplex
                        direction = ac.cross(ao).cross(ac);
                        return false;
                    }else {
                        //check ab to see if its behind a in the dir of b
                        if(ab.dot(ao) > 0) {
                            //also check b since we have NEVER checked behind the third point ever in the building of the simplex
                            //same sign optimization (flip check sign to avoid computing negative of number)
                            if(simplex.points[2].dot(ao) < 0) {
                                //closest 2 points are ab
                                simplex.points[0] = simplex.points[2];
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //do same direction calc
                                direction = (ab.cross(ao)).cross(ab);
                                return false;
                            }else {
                                //closest point is b only
                                simplex.points[0] = simplex.points[2];
                                simplex.n = 1;

                                //direction is simply towards the origin
                                direction = -simplex.points[0];
                                return false;
                            }
                        }else {
                            //its behind a on both ab and ac directions, so a only
                            simplex.points[0] = simplex.points[3];
                            simplex.n = 1;

                            //direction is simply towards the origin
                            direction = -simplex.points[0];

                            return false;
                        }
                    }
                }else {
                    //within the edge
                    //test the other edge to see if within the triangle
                    math::Vec3 edge2 = abcNormal.cross(ab);

                    if(edge2.dot(ao) > 0) {
                        //beyond this edge, do the same ab checks
                        //check ab to see if its behind a in the dir of b
                        if(ab.dot(ao) > 0) {
                            //also check b since we have NEVER checked behind the third point ever in the building of the simplex
                            //same sign optimization (flip check sign to avoid computing negative of number)
                            if(simplex.points[2].dot(ao) < 0) {
                                //closest 2 points are ab
                                simplex.points[0] = simplex.points[2];
                                simplex.points[1] = simplex.points[3];
                                simplex.n = 2;

                                //do same direction calc
                                direction = (ab.cross(ao)).cross(ab);
                                return false;
                            }else {
                                //closest point is b only
                                simplex.points[0] = simplex.points[2];
                                simplex.n = 1;

                                //direction is simply towards the origin
                                direction = -simplex.points[0];
                                return false;
                            }
                        }else {
                            //its behind a on both ab and ac directions, so a only
                            simplex.points[0] = simplex.points[3];
                            simplex.n = 1;

                            //direction is simply towards the origin
                            direction = -simplex.points[0];
                            return false;
                        }
                    }else {
                        //within both edges, and the very first check we did was plane normal
                        //we are outside/below the plane
                        //so winding is incorrect
                        simplex.points[0] = simplex.points[2]; //b becomes oldest
                        //c remains in place (swap functions this way)
                        simplex.points[2] = simplex.points[3]; //a becomes the newest
                        simplex.n = 3;

                        //direciton is the negative (since we need to look the other way
                        direction = -abcNormal;
                        return false;
                    }
                }
            }
            break;
        }
    }

    return false;
}
*/

// ============================================================================
// GJK distance mode — closest-point helpers.
// gjkDistance/updateSimplexDistance (declared in ColliderGJK.h) are the two
// pieces still stubbed; everything below is the supporting barycentric math they
// call once implemented.
// ============================================================================

ClosestPointResult closestPointVertex(const GJKSimplexDistance& simplex) {
    ClosestPointResult r;
    r.point    = simplex.points[0];
    r.onA      = simplex.onA[0];
    r.onB      = simplex.onB[0];
    r.distance = std::sqrt(r.point.dot(r.point));
    return r;
}

ClosestPointResult closestPointLine(const GJKSimplexDistance& simplex) {
    const math::Vec3& A = simplex.points[0];
    const math::Vec3& B = simplex.points[1];
    math::Vec3 AB = B - A;

    float denom = AB.dot(AB);
    float t = (denom > GJK_EPS) ? (-A.dot(AB)) / denom : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);

    ClosestPointResult r;
    r.point    = A + AB * t;
    r.onA      = simplex.onA[0] + (simplex.onA[1] - simplex.onA[0]) * t;
    r.onB      = simplex.onB[0] + (simplex.onB[1] - simplex.onB[0]) * t;
    r.distance = std::sqrt(r.point.dot(r.point));
    return r;
}

ClosestPointResult closestPointTriangle(const GJKSimplexDistance& simplex) {
    const math::Vec3& A = simplex.points[0];
    const math::Vec3& B = simplex.points[1];
    const math::Vec3& C = simplex.points[2];

    // Barycentric coords of the origin's projection onto the triangle's plane
    // (Ericson, Real-Time Collision Detection 3.4 — solving the 2x2 system for
    // expressing (origin - A) in the v0,v1 edge basis).
    math::Vec3 v0 = B - A;
    math::Vec3 v1 = C - A;
    math::Vec3 v2 = -A; // origin - A

    float d00 = v0.dot(v0);
    float d01 = v0.dot(v1);
    float d11 = v1.dot(v1);
    float d20 = v2.dot(v0);
    float d21 = v2.dot(v1);
    float denom = d00 * d11 - d01 * d01;

    float v = 0.0f, w = 0.0f;
    if (std::abs(denom) > GJK_EPS) {
        v = (d11 * d20 - d01 * d21) / denom;
        w = (d00 * d21 - d01 * d20) / denom;
    }
    float u = 1.0f - v - w;

    if (u >= 0.0f && v >= 0.0f && w >= 0.0f) {
        // Projection lands inside the triangle — face weights are the answer.
        ClosestPointResult r;
        r.point    = A * u + B * v + C * w;
        r.onA      = simplex.onA[0] * u + simplex.onA[1] * v + simplex.onA[2] * w;
        r.onB      = simplex.onB[0] * u + simplex.onB[1] * v + simplex.onB[2] * w;
        r.distance = std::sqrt(r.point.dot(r.point));
        return r;
    }

    // Outside the face: fall back to the nearest of the 3 edges (each clamped to
    // its own endpoints by closestPointLine). Simpler than a full vertex/edge
    // Voronoi-region cascade and just as correct — acceptable since these
    // simplices are tiny.
    GJKSimplexDistance edgeAB; edgeAB.n = 2;
    edgeAB.points[0] = A; edgeAB.points[1] = B;
    edgeAB.onA[0] = simplex.onA[0]; edgeAB.onA[1] = simplex.onA[1];
    edgeAB.onB[0] = simplex.onB[0]; edgeAB.onB[1] = simplex.onB[1];

    GJKSimplexDistance edgeAC; edgeAC.n = 2;
    edgeAC.points[0] = A; edgeAC.points[1] = C;
    edgeAC.onA[0] = simplex.onA[0]; edgeAC.onA[1] = simplex.onA[2];
    edgeAC.onB[0] = simplex.onB[0]; edgeAC.onB[1] = simplex.onB[2];

    GJKSimplexDistance edgeBC; edgeBC.n = 2;
    edgeBC.points[0] = B; edgeBC.points[1] = C;
    edgeBC.onA[0] = simplex.onA[1]; edgeBC.onA[1] = simplex.onA[2];
    edgeBC.onB[0] = simplex.onB[1]; edgeBC.onB[1] = simplex.onB[2];

    ClosestPointResult best = closestPointLine(edgeAB);
    ClosestPointResult candAC = closestPointLine(edgeAC);
    if (candAC.distance < best.distance) best = candAC;
    ClosestPointResult candBC = closestPointLine(edgeBC);
    if (candBC.distance < best.distance) best = candBC;

    return best;
}

ClosestPointResult closestPointOnSimplex(const GJKSimplexDistance& simplex) {
    switch (simplex.n) {
        case 1:  return closestPointVertex(simplex);
        case 2:  return closestPointLine(simplex);
        case 3:  return closestPointTriangle(simplex);
        default: return closestPointVertex(simplex); // defensive; n==0 shouldn't occur
    }
}

ClosestPointResult updateSimplexDistance(GJKSimplexDistance& simplex, math::Vec3& direction) {
    // TODO: given the candidate simplex (previous minimal simplex + the freshly
    // added support point, up to 4 points), find the minimal-dimension sub-simplex
    // actually closest to the origin (try the full candidate plus each sub-simplex
    // obtained by dropping one point, keep whichever gives the smallest distance —
    // same "just try them all" spirit as closestPointTriangle's edge fallback
    // above), compact `simplex` (points/onA/onB/n) to match, and set
    // direction = -result.point before returning it.
    //
    // Guard: if simplex.n would need to stay at 4 (candidate already encloses the
    // origin), that means the shapes actually intersect — a contract violation
    // for distance mode (see gjkDistance's caller contract) rather than a case to
    // silently handle here.
    (void)simplex; (void)direction;
    return ClosestPointResult{};
}

// ============================================================================
// GJK debug / test harness (editor oracle + simplex stepper).
// Defined here, AFTER gjkIntersect/updateSimplex, so the templated intersect is
// already visible for instantiation. None of this is GJK logic — it only wraps
// the existing pieces so editor tooling can drive the real algorithm and diff it
// against the proven SAT detect* routines.
// ============================================================================

std::function<math::Vec3(math::Vec3)>
makeSupportFn(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg) {
    // Capture the resolved shapes BY VALUE: the returned closure outlives this
    // call, unlike makeSupport()'s by-reference capture used inside detect().
    ShapeVariant va = makeShapeVariant(ea, reg);
    ShapeVariant vb = makeShapeVariant(eb, reg);
    return [va, vb](math::Vec3 dir) -> math::Vec3 {
        return std::visit([&dir](const auto& a, const auto& b) {
            return supportSingle(a, dir) - supportSingle(b, -dir);
        }, va, vb);
    };
}

// Single source of truth for GJK's seed direction. gjkBoolean (oracle) and
// gjkTrace (stepper) both use this so they reproduce the same first iteration.
// NOTE: detectGJK currently computes its own seed inline — route it through this
// too (one-line change) if you want the stepper to mirror a seed experiment there.
static math::Vec3 gjkInitDir(const ShapeVariant& va, const ShapeVariant& vb) {
    math::Vec3 d = shapePosition(vb) - shapePosition(va);
    if (d.dot(d) < 1e-8f) d = {0.0f, -1.0f, 0.0f};  // coincident centers fallback
    return d;
}

void detectGJK(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg,
            std::vector<ActiveContact>& contacts)
{
    auto* hca = reg.get<ecs::Hull>(ea);
    auto* hcb = reg.get<ecs::Hull>(eb);
    if (!hca || !hcb) return;
    if (!hca->tangible || !hcb->tangible) return;

    bool aFixed = reg.has<ecs::Fixed>(ea);
    bool bFixed = reg.has<ecs::Fixed>(eb);
    if (aFixed && bFixed) return;
    if (!(hca->collisionLayer & hcb->collisionMask) || !(hcb->collisionLayer & hca->collisionMask)) return;

    auto* tfa = reg.get<Transform>(ea);
    auto* tfb = reg.get<Transform>(eb);
    if (!tfa || !tfb) return;

    //prior to shunting, make a unified contact manifold to store the results regardless of technique
    ContactManifold m;

    ShapeVariant va = makeShapeVariant(ea, reg);
    ShapeVariant vb = makeShapeVariant(eb, reg);

    // Shunt: sphere/AABB/OBB pairs use SAT; sphere/capsule/capsule use analytical.
    // Both produce full manifolds directly — no GJK/EPA needed.
    if (isAnalyticalPair(va, vb)) {
        if (analyticalBoolean(va, vb, m)) {
            ActiveContact c; c.a = ea; c.b = eb; c.manifold = m;
            contacts.push_back(c);
        }
        return;
    }

    //determine bucket for profiling
    PairBucket bucket = getBucket((int)va.index(), (int)vb.index());
    NPHASE_TIME(bucket);

    //determine the appropriate support function
    auto supportFunction = makeSupport(va, vb);

    //instantiate the simplex in which gjk results will be stored
    GJKSimplex simplex;
    //compute the initial direction for best convergence to be the difference of the 2 objects
    math::Vec3 initDir = shapePosition(vb) - shapePosition(va);
    //fallback if 0 to just basic down
    float      lenSq   = initDir.dot(initDir);
    if (lenSq < 1e-8f) initDir = {0.0f, -1.0f, 0.0f}; // coincident centers fallback

    //detect with gjk
    if (!gjkIntersect(supportFunction, simplex, initDir)) return;

    //make the manifold with epa from gjk result
    epaManifold(supportFunction, simplex, m);
}

bool gjkBoolean(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg, GJKSimplex* outSimplex) {
    auto* tfa = reg.get<Transform>(ea);
    auto* tfb = reg.get<Transform>(eb);
    if (!tfa || !tfb) return false;

    ShapeVariant va = makeShapeVariant(ea, reg);
    ShapeVariant vb = makeShapeVariant(eb, reg);

    ContactManifold m;

    // Shunt analytical pairs (sphere-sphere, sphere-capsule, capsule-capsule)
    if (isAnalyticalPair(va, vb)) return analyticalBoolean(va, vb, m);

    auto supportFunction = makeSupport(va, vb);

    GJKSimplex simplex;
    math::Vec3 initDir = gjkInitDir(va, vb);

    bool hit = gjkIntersect(supportFunction, simplex, initDir);
    if (outSimplex) *outSimplex = simplex;
    return hit;
}

bool gjkTrace(ecs::Entity ea, ecs::Entity eb, ecs::Registry& reg, GJKTrace& outTrace) {
    outTrace.clear();
    auto* tfa = reg.get<Transform>(ea);
    auto* tfb = reg.get<Transform>(eb);
    if (!tfa || !tfb) return false;

    ShapeVariant va = makeShapeVariant(ea, reg);
    ShapeVariant vb = makeShapeVariant(eb, reg);
    auto supportFunction = makeSupport(va, vb);

    GJKSimplex simplex;
    math::Vec3 initDir = gjkInitDir(va, vb);
    // Runs the REAL templated gjkIntersect with recording on — the stepper then
    // scrubs outTrace, so it can never drift from the actual loop again.
    return gjkIntersect(supportFunction, simplex, initDir, &outTrace);
}

} // namespace ColliderDiscrete
} // namespace physics
