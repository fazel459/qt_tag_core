#include "SimulatorDriver.h"

#include <cmath>

#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>

#include "../scaling/ScalingEngine.h"

namespace
{
    constexpr double PI = 3.14159265358979323846;
}

SimulatorDriver::SimulatorDriver(
    TagBus& bus,
    const QVector<TagDefinition>& tags,

    QObject* parent
)
    : QObject(parent)
    , m_bus(bus)
    , m_tags(tags)

{

    m_timer.setInterval(1000);
    m_timer.setParent(this);

    QObject::connect(&m_timer, &QTimer::timeout, [this]()
    {
        tick();
    });
}

void SimulatorDriver::start()
{
    qInfo() << "SimulatorDriver started with" << m_tags.size() << "tags";
    m_timer.start();
}

void SimulatorDriver::stop()
{
    m_timer.stop();
}

void SimulatorDriver::tick()
{
    for (const TagDefinition& tag : m_tags)
    {
        if (!tag.enabled)
        {
            continue;
        }

        Quality quality = Quality::Good;

        if (tag.simProfile == "bad_quality")
        {
            quality = Quality::Bad;
        }

        const double raw = generateRaw(tag);

        const double scaled = ScalingEngine::scale(tag, raw);
        TagValue value;
        value.tagId = tag.tagId;
        value.tagName = tag.tagName;
        value.timestamp = QDateTime::currentDateTimeUtc();

        value.rawValue = raw;
        value.engineeringValue = scaled;

        value.quality = quality;
        value.source = SourceKind::Simulator;
        value.sequence = ++m_sequence;


        const QString topic = QStringLiteral("tags/%1/raw").arg(tag.tagId);

        m_bus.publish(topic, value);

        qInfo().noquote()
            << "SIM"
            << value.timestamp.toString(Qt::ISODate)
            << value.tagName
            << "raw=" << value.rawValue
            << "eng=" << value.engineeringValue
            << "quality=" << static_cast<int>(value.quality);
    }
}

double SimulatorDriver::generateRaw(const TagDefinition& tag)
{
    const QString profile = tag.simProfile;

    if (profile == "sine")
    {
        const double mid = (tag.rawMax + tag.rawMin) / 2.0;
        const double amplitude = (tag.rawMax - tag.rawMin) / 2.0;

        double& phase = m_phase[tag.tagId];

        const double raw = mid + amplitude * std::sin(phase);

        phase += 0.10;

        if (phase > 2.0 * PI)
        {
            phase -= 2.0 * PI;
        }

        return raw;
    }

    if (profile == "random")
    {
        const double randomFraction = QRandomGenerator::global()->generateDouble();
        return tag.rawMin + (tag.rawMax - tag.rawMin) * randomFraction;
    }

    if (profile == "ramp")
    {
        const double span = tag.rawMax - tag.rawMin;
        const double step = span / 50.0;

        if (!m_ramp.contains(tag.tagId))
        {
            m_ramp.insert(tag.tagId, tag.rawMin);
            return tag.rawMin;
        }

        double current = m_ramp.value(tag.tagId);
        current += step;

        if (current > tag.rawMax)
        {
            current = tag.rawMin;
        }

        m_ramp.insert(tag.tagId, current);

        return current;
    }

    if (profile == "stuck")
    {
        return (tag.rawMax + tag.rawMin) / 2.0;
    }

    if (profile == "bad_quality")
    {
        return (tag.rawMax + tag.rawMin) / 2.0;
    }

    return tag.rawMin;
}
