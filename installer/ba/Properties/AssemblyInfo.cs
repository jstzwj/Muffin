using System.Reflection;
using System.Windows;
using Muffin.Setup;
using WixToolset.Mba.Core;

[assembly: AssemblyTitle("MuffinBootstrapperUI")]
[assembly: AssemblyDescription("Muffin setup bootstrapper application")]
[assembly: AssemblyProduct("Muffin")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: AssemblyFileVersion("1.0.0.0")]

[assembly: ThemeInfo(
    ResourceDictionaryLocation.None,
    ResourceDictionaryLocation.SourceAssembly)]

// Burn discovers the BA through this attribute: the mbanative host reads it to
// find the factory that creates our BootstrapperApplication subclass. WiX v3
// pointed this at the BA class itself; v4 requires the factory.
[assembly: BootstrapperApplicationFactory(typeof(MuffinBaFactory))]
