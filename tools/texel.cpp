#include "evaluate.h"
#include "movegen.h"
#include "position.h"
#include "texel_params.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace
{
    struct Sample
    {
        std::string fen;
        double result;
    };

    struct Term
    {
        uint32_t index;
        float coeff;
    };

    struct Linear
    {
        std::vector<double> base;
        std::vector<uint64_t> offsets;
        std::vector<Term> terms;
    };

    struct Settings
    {
        std::string dataPath;
        std::string outPath;
        std::string workDir;
        int threads = 1;
        int rounds = 2;
        int epochs = 400;
        double lr = 0.5;
        int step = 4;
        double k = 0.0;
    };

    std::vector<int *> slots;

    void collectSlots()
    {
        for (const TexelParam &param : texelParams)
            for (int i = 0; i < param.count; ++i)
                slots.push_back(param.data + i);
    }

    double whiteEval(const Position &pos)
    {
        const int value = evaluate::hce(pos);
        return pos.sideToMove == WHITE ? value : -value;
    }

    std::vector<Sample> loadSamples(const std::string &path)
    {
        std::vector<Sample> samples;
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line))
        {
            std::istringstream ss(line);
            Sample sample;
            if (!(ss >> sample.result))
                continue;
            std::getline(ss >> std::ws, sample.fen);
            if (!sample.fen.empty())
                samples.push_back(sample);
        }
        return samples;
    }

    void linearizeRange(const std::vector<Sample> &samples, size_t begin, size_t end, int step, const std::string &path)
    {
        FILE *out = std::fopen(path.c_str(), "wb");
        if (out == nullptr)
            std::_Exit(2);

        Position pos;
        std::vector<Term> terms;
        for (size_t i = begin; i < end; ++i)
        {
            terms.clear();
            double base = 0.0;
            if (pos.set_fen(samples[i].fen))
            {
                base = whiteEval(pos);
                for (size_t j = 0; j < slots.size(); ++j)
                {
                    const int original = *slots[j];
                    *slots[j] = original + step;
                    const double plus = whiteEval(pos);
                    *slots[j] = original - step;
                    const double minus = whiteEval(pos);
                    *slots[j] = original;
                    const double coeff = (plus - minus) / (2.0 * step);
                    if (coeff != 0.0)
                        terms.push_back({static_cast<uint32_t>(j), static_cast<float>(coeff)});
                }
            }
            const uint32_t count = static_cast<uint32_t>(terms.size());
            std::fwrite(&base, sizeof(base), 1, out);
            std::fwrite(&count, sizeof(count), 1, out);
            if (count > 0)
                std::fwrite(terms.data(), sizeof(Term), count, out);
        }
        std::fclose(out);
    }

    bool readShard(const std::string &path, Linear &linear)
    {
        FILE *in = std::fopen(path.c_str(), "rb");
        if (in == nullptr)
            return false;

        double base = 0.0;
        uint32_t count = 0;
        while (std::fread(&base, sizeof(base), 1, in) == 1)
        {
            if (std::fread(&count, sizeof(count), 1, in) != 1)
                break;
            linear.base.push_back(base);
            const size_t at = linear.terms.size();
            linear.terms.resize(at + count);
            if (count > 0 && std::fread(linear.terms.data() + at, sizeof(Term), count, in) != count)
                break;
            linear.offsets.push_back(linear.terms.size());
        }
        std::fclose(in);
        std::remove(path.c_str());
        return true;
    }

    Linear linearize(const std::vector<Sample> &samples, const Settings &settings)
    {
        const size_t total = samples.size();
        const size_t workers = static_cast<size_t>(std::max(1, settings.threads));
        std::vector<pid_t> children;
        std::vector<std::string> shards;

        for (size_t w = 0; w < workers; ++w)
        {
            const size_t begin = total * w / workers;
            const size_t end = total * (w + 1) / workers;
            const std::string shard = settings.workDir + "/shard_" + std::to_string(w) + ".bin";
            shards.push_back(shard);

            const pid_t pid = fork();
            if (pid == 0)
            {
                linearizeRange(samples, begin, end, settings.step, shard);
                std::_Exit(0);
            }
            if (pid < 0)
            {
                linearizeRange(samples, begin, end, settings.step, shard);
                continue;
            }
            children.push_back(pid);
        }

        for (const pid_t pid : children)
        {
            int status = 0;
            waitpid(pid, &status, 0);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            {
                std::cerr << "linearization worker failed" << std::endl;
                std::exit(1);
            }
        }

        Linear linear;
        linear.offsets.push_back(0);
        for (const std::string &shard : shards)
            if (!readShard(shard, linear))
            {
                std::cerr << "missing shard " << shard << std::endl;
                std::exit(1);
            }

        if (linear.base.size() != total)
        {
            std::cerr << "shard size mismatch" << std::endl;
            std::exit(1);
        }
        return linear;
    }

    double sigmoid(double k, double eval)
    {
        return 1.0 / (1.0 + std::exp(-k * eval));
    }

    double lossOf(const std::vector<Sample> &samples, const std::vector<double> &evals, double k)
    {
        double sum = 0.0;
        for (size_t i = 0; i < samples.size(); ++i)
        {
            const double error = samples[i].result - sigmoid(k, evals[i]);
            sum += error * error;
        }
        return sum / static_cast<double>(samples.size());
    }

    std::vector<double> trueEvals(const std::vector<Sample> &samples)
    {
        std::vector<double> evals(samples.size(), 0.0);
        Position pos;
        for (size_t i = 0; i < samples.size(); ++i)
            if (pos.set_fen(samples[i].fen))
                evals[i] = whiteEval(pos);
        return evals;
    }

    double fitK(const std::vector<Sample> &samples, const std::vector<double> &evals)
    {
        double low = 0.0001;
        double high = 0.05;
        for (int i = 0; i < 100; ++i)
        {
            const double a = low + (high - low) / 3.0;
            const double b = high - (high - low) / 3.0;
            if (lossOf(samples, evals, a) < lossOf(samples, evals, b))
                high = b;
            else
                low = a;
        }
        return (low + high) / 2.0;
    }

    void optimize(const std::vector<Sample> &samples, const Linear &linear, std::vector<double> &theta,
                  const std::vector<double> &origin, const Settings &settings, double k)
    {
        constexpr double BETA1 = 0.9;
        constexpr double BETA2 = 0.999;
        constexpr double EPSILON = 1e-8;

        const size_t count = theta.size();
        std::vector<double> m(count, 0.0);
        std::vector<double> v(count, 0.0);
        std::vector<double> grad(count, 0.0);
        std::vector<double> delta(count, 0.0);
        const double scale = 1.0 / static_cast<double>(samples.size());

        for (int epoch = 1; epoch <= settings.epochs; ++epoch)
        {
            for (size_t j = 0; j < count; ++j)
                delta[j] = theta[j] - origin[j];

            std::fill(grad.begin(), grad.end(), 0.0);
            double loss = 0.0;
            for (size_t i = 0; i < samples.size(); ++i)
            {
                double eval = linear.base[i];
                for (uint64_t t = linear.offsets[i]; t < linear.offsets[i + 1]; ++t)
                    eval += linear.terms[t].coeff * delta[linear.terms[t].index];

                const double s = sigmoid(k, eval);
                const double error = samples[i].result - s;
                loss += error * error;
                const double g = -2.0 * error * s * (1.0 - s) * k * scale;
                for (uint64_t t = linear.offsets[i]; t < linear.offsets[i + 1]; ++t)
                    grad[linear.terms[t].index] += g * linear.terms[t].coeff;
            }

            const double correction1 = 1.0 - std::pow(BETA1, epoch);
            const double correction2 = 1.0 - std::pow(BETA2, epoch);
            for (size_t j = 0; j < count; ++j)
            {
                m[j] = BETA1 * m[j] + (1.0 - BETA1) * grad[j];
                v[j] = BETA2 * v[j] + (1.0 - BETA2) * grad[j] * grad[j];
                theta[j] -= settings.lr * (m[j] / correction1) / (std::sqrt(v[j] / correction2) + EPSILON);
            }

            if (epoch == 1 || epoch % 50 == 0 || epoch == settings.epochs)
                std::cout << "  epoch " << epoch << " linear loss " << std::setprecision(8) << loss * scale << std::endl;
        }
    }

    void writeValues(const std::string &path)
    {
        std::ofstream out(path);
        for (const TexelParam &param : texelParams)
        {
            out << param.name;
            for (int i = 0; i < param.count; ++i)
                out << ' ' << param.data[i];
            out << '\n';
        }
    }

    bool parseSettings(int argc, char **argv, Settings &settings)
    {
        if (argc != 11)
            return false;
        settings.dataPath = argv[1];
        settings.outPath = argv[2];
        settings.workDir = argv[3];
        settings.threads = std::atoi(argv[4]);
        settings.rounds = std::atoi(argv[5]);
        settings.epochs = std::atoi(argv[6]);
        settings.lr = std::atof(argv[7]);
        settings.step = std::max(1, std::atoi(argv[8]));
        settings.k = std::atof(argv[9]);
        return std::string(argv[10]) == "run";
    }
}

int main(int argc, char **argv)
{
    Settings settings;
    if (!parseSettings(argc, argv, settings))
    {
        std::cerr << "usage: texel data out workdir threads rounds epochs lr step k run" << std::endl;
        return 1;
    }

    movegen::init();
    collectSlots();

    const std::vector<Sample> samples = loadSamples(settings.dataPath);
    if (samples.empty())
    {
        std::cerr << "no samples in " << settings.dataPath << std::endl;
        return 1;
    }
    std::cout << "positions " << samples.size() << " parameters " << slots.size() << std::endl;

    double k = settings.k;
    std::vector<double> evals = trueEvals(samples);
    if (k <= 0.0)
        k = fitK(samples, evals);
    const double startLoss = lossOf(samples, evals, k);
    std::cout << "k " << std::setprecision(8) << k << " start loss " << startLoss << std::endl;

    double bestLoss = startLoss;
    std::vector<int> best(slots.size());
    for (size_t j = 0; j < slots.size(); ++j)
        best[j] = *slots[j];

    for (int round = 1; round <= settings.rounds; ++round)
    {
        std::cout << "round " << round << " linearizing" << std::endl;
        const Linear linear = linearize(samples, settings);

        std::vector<double> origin(slots.size());
        for (size_t j = 0; j < slots.size(); ++j)
            origin[j] = *slots[j];
        std::vector<double> theta = origin;

        optimize(samples, linear, theta, origin, settings, k);

        for (size_t j = 0; j < slots.size(); ++j)
            *slots[j] = static_cast<int>(std::lround(theta[j]));

        evals = trueEvals(samples);
        const double loss = lossOf(samples, evals, k);
        std::cout << "round " << round << " true loss " << std::setprecision(8) << loss << std::endl;

        if (loss < bestLoss)
        {
            bestLoss = loss;
            for (size_t j = 0; j < slots.size(); ++j)
                best[j] = *slots[j];
        }
        else
        {
            for (size_t j = 0; j < slots.size(); ++j)
                *slots[j] = best[j];
            std::cout << "round " << round << " did not improve, keeping the previous values" << std::endl;
            break;
        }
    }

    for (size_t j = 0; j < slots.size(); ++j)
        *slots[j] = best[j];
    writeValues(settings.outPath);
    std::cout << "final loss " << std::setprecision(8) << bestLoss << " (start " << startLoss << ")" << std::endl;
    return 0;
}
