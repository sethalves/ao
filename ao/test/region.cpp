#include "catch.hpp"

#include "ao/render/brep/region.hpp"
#include "ao/render/axes.hpp"

using namespace Kernel;

TEST_CASE("Region<2>::subdivide")
{
    SECTION("On all axes")
    {
        Region<2> a({-1, -2}, {1, 2});
        auto out = a.subdivide();
        REQUIRE(out.size() == 4);

        for (int i=0; i < 4; ++i)
        {
            const auto& sub = out[i];
            CAPTURE(sub.lower.x());
            CAPTURE(sub.upper.x());
            CAPTURE(sub.lower.y());
            CAPTURE(sub.upper.y());
            if (i & Axis::X)
            {
                REQUIRE(sub.lower.x() ==  0);
                REQUIRE(sub.upper.x() ==  1);
            }
            else
            {
                REQUIRE(sub.lower.x() == -1);
                REQUIRE(sub.upper.x() ==  0);
            }

            if (i & Axis::Y)
            {
                REQUIRE(sub.lower.y() ==  0);
                REQUIRE(sub.upper.y() ==  2);
            }
            else
            {
                REQUIRE(sub.lower.y() == -2);
                REQUIRE(sub.upper.y() ==  0);
            }

            REQUIRE(!(i & Axis::Z));
        }
    }

    SECTION("With perp")
    {
        Region<2> a({-1, -2}, {1, 2}, Region<2>::Perp(10));
        auto out = a.subdivide();

        for (int i=0; i < 4; ++i)
        {
            CAPTURE(out[i].perp);
            REQUIRE((out[i].perp == Region<2>::Perp(10)).all());
        }
    }
}

TEST_CASE("Region<3>::subdivide")
{
    Region<3> a({-1, -2, -4}, {1, 2, 4});

    SECTION("On all axes")
    {
        auto out = a.subdivide();

        REQUIRE(out.size() == 8);
        for (int i=0; i < 8; ++i)
        {
            const auto& sub = out[i];
            CAPTURE(sub.lower.x());
            CAPTURE(sub.upper.x());
            CAPTURE(sub.lower.y());
            CAPTURE(sub.upper.y());
            CAPTURE(sub.lower.z());
            CAPTURE(sub.upper.z());
            if (i & Axis::X)
            {
                REQUIRE(sub.lower.x() ==  0);
                REQUIRE(sub.upper.x() ==  1);
            }
            else
            {
                REQUIRE(sub.lower.x() == -1);
                REQUIRE(sub.upper.x() ==  0);
            }

            if (i & Axis::Y)
            {
                REQUIRE(sub.lower.y() ==  0);
                REQUIRE(sub.upper.y() ==  2);
            }
            else
            {
                REQUIRE(sub.lower.y() == -2);
                REQUIRE(sub.upper.y() ==  0);
            }

            if (i & Axis::Z)
            {
                REQUIRE(sub.lower.z() ==  0);
                REQUIRE(sub.upper.z() ==  4);
            }
            else
            {
                REQUIRE(sub.lower.z() == -4);
                REQUIRE(sub.upper.z() ==  0);
            }
        }
    }
}
