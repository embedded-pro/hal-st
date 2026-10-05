#include "validation/firmware/PkaGroup.hpp"
#include "services/crypto/Secp256r1.hpp"
#include <algorithm>
#include <chrono>

namespace validation
{
    namespace
    {
        using services::HilStatus;
        using PointOnCurveResult = services::EllipticCurveOperations::PointOnCurveResult;
        using ComparisonResult = services::EllipticCurveOperations::ComparisonResult;

        constexpr std::size_t minimumComparison = 4;
        constexpr std::chrono::milliseconds timeout{ 5000 };

        bool IsHexDigit(char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }

        bool HexSize(const std::optional<infra::BoundedConstString>& text, std::size_t& size)
        {
            if (!text || text->empty() || text->size() % 2 != 0 || !std::all_of(text->begin(), text->end(), IsHexDigit))
                return false;

            size = text->size() / 2;
            return true;
        }

        template<std::size_t N>
        void LoadPadded(infra::BoundedConstString text, std::array<uint8_t, N>& operand)
        {
            std::size_t size = 0;
            operand.fill(0);
            services::HilArguments::ParseHex(text, infra::Tail(infra::MakeRange(operand), text.size() / 2), size);
        }
    }

    PkaCommands::PkaCommands(services::HilContext& context)
        : services::TerminalCommands(context.terminal)
        , context(context)
        , pending(context.response)
        , commands{ {
              services::HilBind<PkaCommands, &PkaCommands::Multiply>("pka.mul", "[k=<hex>] [x=<hex> y=<hex>]", *this, context.response),
              services::HilBind<PkaCommands, &PkaCommands::CheckPoint>("pka.check", "x=<hex> y=<hex>", *this, context.response),
              services::HilBind<PkaCommands, &PkaCommands::Compare>("pka.cmp", "a=<hex> b=<hex>", *this, context.response),
          } }
    {}

    infra::MemoryRange<const PkaCommands::Command> PkaCommands::Commands()
    {
        return infra::MakeRange(commands);
    }

    HilStatus PkaCommands::Multiply(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, { "k", "x", "y" }))
            return HilStatus::usage;

        auto k = arguments.Key("k");
        auto pointX = arguments.Key("x");
        auto pointY = arguments.Key("y");
        std::size_t kSize = 0;
        std::size_t xSize = 0;
        std::size_t ySize = 0;
        if (pointX.has_value() != pointY.has_value() || (k && !HexSize(k, kSize)) || (pointX && (!HexSize(pointX, xSize) || !HexSize(pointY, ySize))))
            return HilStatus::usage;

        if (std::max({ kSize, xSize, ySize }) > scalar.size())
            return HilStatus::range;

        auto status = Reserve();
        if (status != HilStatus::done)
            return status;

        if (k)
            LoadPadded(*k, scalar);
        else
        {
            scalar.fill(0);
            scalar.back() = 1;
        }

        infra::ConstByteRange initialX;
        infra::ConstByteRange initialY;
        if (pointX)
        {
            LoadPadded(*pointX, x);
            LoadPadded(*pointY, y);
            initialX = infra::MakeRange(x);
            initialY = infra::MakeRange(y);
        }

        auto& driver = Pka();
        driver.ScalarMultiplication(services::secp256r1, infra::MakeRange(scalar), initialX, initialY, [this, operation = Start()](infra::ConstByteRange resultX, infra::ConstByteRange resultY)
            {
                auto elapsed = stopwatch.ElapsedUs();

                if (!pending.Complete(operation))
                    return;

                auto line = context.response.Ok();
                line << " x=";
                line.Hex(resultX);
                line << " y=";
                line.Hex(resultY);
                line << " us=" << elapsed;
            });

        return HilStatus::done;
    }

    HilStatus PkaCommands::CheckPoint(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, { "x", "y" }))
            return HilStatus::usage;

        auto pointX = arguments.Key("x");
        auto pointY = arguments.Key("y");
        std::size_t xSize = 0;
        std::size_t ySize = 0;
        if (!HexSize(pointX, xSize) || !HexSize(pointY, ySize))
            return HilStatus::usage;

        if (std::max(xSize, ySize) > x.size())
            return HilStatus::range;

        auto status = Reserve();
        if (status != HilStatus::done)
            return status;

        LoadPadded(*pointX, x);
        LoadPadded(*pointY, y);

        auto& driver = Pka();
        driver.CheckPointOnCurve(services::secp256r1, infra::MakeRange(x), infra::MakeRange(y), [this, operation = Start()](PointOnCurveResult result)
            {
                if (pending.Complete(operation))
                    context.response.Ok() << " on=" << static_cast<uint32_t>(result == PointOnCurveResult::pointOnCurve ? 1 : 0);
            });

        return HilStatus::done;
    }

    HilStatus PkaCommands::Compare(const services::HilArguments& arguments)
    {
        if (!arguments.Shape(0, 0, { "a", "b" }))
            return HilStatus::usage;

        auto textA = arguments.Key("a");
        auto textB = arguments.Key("b");
        std::size_t size = 0;
        std::size_t sizeB = 0;
        if (!HexSize(textA, size) || !HexSize(textB, sizeB) || size != sizeB)
            return HilStatus::usage;

        if (size < minimumComparison || size > a.size())
            return HilStatus::range;

        auto status = Reserve();
        if (status != HilStatus::done)
            return status;

        services::HilArguments::ParseHex(*textA, infra::MakeRange(a), sizeB);
        services::HilArguments::ParseHex(*textB, infra::MakeRange(b), sizeB);

        auto& driver = Pka();
        driver.Comparison(infra::Head(infra::MakeRange(a), size), infra::Head(infra::MakeRange(b), size), [this, operation = Start()](ComparisonResult result)
            {
                if (!pending.Complete(operation))
                    return;

                const char* name = "lt";
                if (result == ComparisonResult::aEqualsB)
                    name = "eq";
                else if (result == ComparisonResult::aGreaterThanB)
                    name = "gt";

                context.response.Ok() << " cmp=" << name;
            });

        return HilStatus::done;
    }

    HilStatus PkaCommands::Reserve()
    {
        if (!pending.Busy())
            return HilStatus::done;

        if (infra::Now() < deadline)
            return HilStatus::busy;

        // An operation whose ERR timeout went out may never complete; the next command may start another one
        pending.Cancel();
        return HilStatus::done;
    }

    uint32_t PkaCommands::Start()
    {
        deadline = infra::Now() + timeout;
        stopwatch.Start();
        return pending.Start(timeout);
    }

    const hal::PkaStm& PkaCommands::Pka()
    {
        // Built on first use and kept: PkaStm::Enable waits for the PKA without a timeout, which must not stall the boot
        if (!pka)
            pka.emplace();

        return *pka;
    }
}
