using WixToolset.Mba.Core;

namespace Muffin.Setup
{
    public class MuffinBaFactory : BaseBootstrapperApplicationFactory
    {
        protected override IBootstrapperApplication Create(IEngine engine, IBootstrapperCommand command)
        {
            return new MuffinBa(engine, command);
        }
    }
}
