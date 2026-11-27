#include "material_proxies.h"
#include "material_system.h"
#include "tier1/keyvalues.h"

#include <stdexcept>

namespace
{
void Check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
}

int main()
{
    KeyValues kv("material");
    Check(kv.LoadFromBuffer("proxy-test.vmt", R"VMT(
        VertexLitGeneric
        {
            $alpha 1
            $low 0.25
            $high 0.75
            $yellow 0
            $tint "[0.25 0.5 0.75]"
            $unavailable 0
            Proxies
            {
                YellowLevel { resultVar $yellow }
                Multiply { srcVar1 $tint srcVar2 $yellow resultVar $color2 }
                Equals { srcVar1 $high resultVar $alpha }
                Equals { srcVar1 $low resultVar $alpha }
                GameOnlyProxy { resultVar $unavailable }
                Multiply { srcVar1 $unavailable srcVar2 $tint resultVar $color2 }
                TextureScroll { textureScrollVar $basetexturetransform textureScrollRate -0.5 }
                AnimatedTexture { animatedTextureVar $basetexture animatedTextureFrameNumVar $frame animatedTextureFrameRate 4 }
            }
        }
    )VMT"), "material fixture must parse");

    Material material;
    material.proxies = std::make_unique<MaterialProxies>(kv, [](const QString &key) {
        return key == QStringLiteral("$basetexture") ? 3 : 0;
    });
    material.blend = MaterialBlend::Add;
    material.UpdateProxies(.5);
    Check(material.ProxyValue("$color2") == QVector4D(.25, .5, .75, 1), "game-only output clobbered the neutral TF tint");
    Check(material.ProxyValue("$alpha") == QVector4D(.25, .25, .25, .25) && material.effectiveBlend() == MaterialBlend::BlendAdd,
          "repeated proxies did not run in file order or proxy alpha did not affect blending");
    const auto uv = material.ProxyTransform("$basetexturetransform");
    Check(uv && uv->row(0) == QVector4D(1, 0, 0, .75),
          "negative texture scroll did not wrap UV");
    Check(material.proxies->intValue("$frame") == 2, "wrong VTF frame at 0.5s");
    material.UpdateProxies(.75);
    Check(material.proxies->intValue("$frame") == 0, "VTF frame did not wrap");
    Check(material.proxies->unsupported().size() == 2, "unsupported proxy or dependent write was not reported");
}
