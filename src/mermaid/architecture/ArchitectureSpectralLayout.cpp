// Spectral initializer ported from cytoscape-fcose 2.2.0 and layout-base
// 2.0.1. The fCoSE code is MIT licensed by the i-Vis Research Group. The
// JAMA-derived SVD implementation in layout-base is Apache-2.0 licensed.
#include "mermaid/architecture/ArchitectureSpectralLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace muffin::mermaid::architecture {
namespace {

using Vector = QVector<double>;
using Matrix = QVector<Vector>;

double stableHypot(double a, double b) {
  if (std::abs(a) > std::abs(b)) {
    const double ratio = b / a;
    return std::abs(a) * std::sqrt(1.0 + ratio * ratio);
  }
  if (b != 0.0) {
    const double ratio = a / b;
    return std::abs(b) * std::sqrt(1.0 + ratio * ratio);
  }
  return 0.0;
}

struct SvdResult {
  Matrix u;
  Matrix v;
  Vector singular;
};

SvdResult svd(Matrix a) {
  const int m = a.size();
  const int n = a.at(0).size();
  const int nu = std::min(m, n);
  Vector s(std::min(m + 1, n), 0.0);
  Matrix u(m, Vector(nu, 0.0));
  Matrix v(n, Vector(n, 0.0));
  Vector e(n, 0.0);
  Vector work(m, 0.0);
  const int nct = std::min(m - 1, n);
  const int nrt = std::max(0, std::min(n - 2, m));

  for (int k = 0; k < std::max(nct, nrt); ++k) {
    if (k < nct) {
      s[k] = 0.0;
      for (int i = k; i < m; ++i) s[k] = stableHypot(s[k], a[i][k]);
      if (s[k] != 0.0) {
        if (a[k][k] < 0.0) s[k] = -s[k];
        for (int i = k; i < m; ++i) a[i][k] /= s[k];
        a[k][k] += 1.0;
      }
      s[k] = -s[k];
    }
    for (int j = k + 1; j < n; ++j) {
      if (k < nct && s[k] != 0.0) {
        double t = 0.0;
        for (int i = k; i < m; ++i) t += a[i][k] * a[i][j];
        t = -t / a[k][k];
        for (int i = k; i < m; ++i) a[i][j] += t * a[i][k];
      }
      e[j] = a[k][j];
    }
    if (k < nct)
      for (int i = k; i < m; ++i) u[i][k] = a[i][k];
    if (k < nrt) {
      e[k] = 0.0;
      for (int i = k + 1; i < n; ++i) e[k] = stableHypot(e[k], e[i]);
      if (e[k] != 0.0) {
        if (e[k + 1] < 0.0) e[k] = -e[k];
        for (int i = k + 1; i < n; ++i) e[i] /= e[k];
        e[k + 1] += 1.0;
      }
      e[k] = -e[k];
      if (k + 1 < m && e[k] != 0.0) {
        for (int i = k + 1; i < m; ++i) work[i] = 0.0;
        for (int j = k + 1; j < n; ++j)
          for (int i = k + 1; i < m; ++i) work[i] += e[j] * a[i][j];
        for (int j = k + 1; j < n; ++j) {
          const double t = -e[j] / e[k + 1];
          for (int i = k + 1; i < m; ++i) a[i][j] += t * work[i];
        }
      }
      for (int i = k + 1; i < n; ++i) v[i][k] = e[i];
    }
  }

  int p = std::min(n, m + 1);
  if (nct < n) s[nct] = a[nct][nct];
  if (m < p) s[p - 1] = 0.0;
  if (nrt + 1 < p) e[nrt] = a[nrt][p - 1];
  e[p - 1] = 0.0;

  for (int j = nct; j < nu; ++j) {
    for (int i = 0; i < m; ++i) u[i][j] = 0.0;
    u[j][j] = 1.0;
  }
  for (int k = nct - 1; k >= 0; --k) {
    if (s[k] != 0.0) {
      for (int j = k + 1; j < nu; ++j) {
        double t = 0.0;
        for (int i = k; i < m; ++i) t += u[i][k] * u[i][j];
        t = -t / u[k][k];
        for (int i = k; i < m; ++i) u[i][j] += t * u[i][k];
      }
      for (int i = k; i < m; ++i) u[i][k] = -u[i][k];
      u[k][k] = 1.0 + u[k][k];
      for (int i = 0; i < k - 1; ++i) u[i][k] = 0.0;
    } else {
      for (int i = 0; i < m; ++i) u[i][k] = 0.0;
      u[k][k] = 1.0;
    }
  }
  for (int k = n - 1; k >= 0; --k) {
    if (k < nrt && e[k] != 0.0) {
      for (int j = k + 1; j < nu; ++j) {
        double t = 0.0;
        for (int i = k + 1; i < n; ++i) t += v[i][k] * v[i][j];
        t = -t / v[k + 1][k];
        for (int i = k + 1; i < n; ++i) v[i][j] += t * v[i][k];
      }
    }
    for (int i = 0; i < n; ++i) v[i][k] = 0.0;
    v[k][k] = 1.0;
  }

  const int pp = p - 1;
  const double eps = std::pow(2.0, -52.0);
  const double tiny = std::pow(2.0, -966.0);
  while (p > 0) {
    int k = p - 2;
    for (; k >= -1; --k) {
      if (k == -1) break;
      if (std::abs(e[k]) <= tiny + eps * (std::abs(s[k]) + std::abs(s[k + 1]))) {
        e[k] = 0.0;
        break;
      }
    }
    int kase;
    if (k == p - 2) {
      kase = 4;
    } else {
      int ks = p - 1;
      for (; ks >= k; --ks) {
        if (ks == k) break;
        const double t = (ks != p ? std::abs(e[ks]) : 0.0) +
                         (ks != k + 1 ? std::abs(e[ks - 1]) : 0.0);
        if (std::abs(s[ks]) <= tiny + eps * t) {
          s[ks] = 0.0;
          break;
        }
      }
      if (ks == k) kase = 3;
      else if (ks == p - 1) kase = 1;
      else { kase = 2; k = ks; }
    }
    ++k;

    if (kase == 1) {
      double f = e[p - 2];
      e[p - 2] = 0.0;
      for (int j = p - 2; j >= k; --j) {
        double t = stableHypot(s[j], f);
        const double cs = s[j] / t;
        const double sn = f / t;
        s[j] = t;
        if (j != k) { f = -sn * e[j - 1]; e[j - 1] = cs * e[j - 1]; }
        for (int i = 0; i < n; ++i) {
          t = cs * v[i][j] + sn * v[i][p - 1];
          v[i][p - 1] = -sn * v[i][j] + cs * v[i][p - 1];
          v[i][j] = t;
        }
      }
    } else if (kase == 2) {
      double f = e[k - 1];
      e[k - 1] = 0.0;
      for (int j = k; j < p; ++j) {
        double t = stableHypot(s[j], f);
        const double cs = s[j] / t;
        const double sn = f / t;
        s[j] = t;
        f = -sn * e[j];
        e[j] = cs * e[j];
        for (int i = 0; i < m; ++i) {
          t = cs * u[i][j] + sn * u[i][k - 1];
          u[i][k - 1] = -sn * u[i][j] + cs * u[i][k - 1];
          u[i][j] = t;
        }
      }
    } else if (kase == 3) {
      const double scale = std::max({std::abs(s[p - 1]), std::abs(s[p - 2]),
                                     std::abs(e[p - 2]), std::abs(s[k]),
                                     std::abs(e[k])});
      const double sp = s[p - 1] / scale;
      const double spm1 = s[p - 2] / scale;
      const double epm1 = e[p - 2] / scale;
      const double sk = s[k] / scale;
      const double ek = e[k] / scale;
      const double b = ((spm1 + sp) * (spm1 - sp) + epm1 * epm1) / 2.0;
      const double c = sp * epm1 * (sp * epm1);
      double shift = 0.0;
      if (b != 0.0 || c != 0.0) {
        shift = std::sqrt(b * b + c);
        if (b < 0.0) shift = -shift;
        shift = c / (b + shift);
      }
      double f = (sk + sp) * (sk - sp) + shift;
      double g = sk * ek;
      for (int j = k; j < p - 1; ++j) {
        double t = stableHypot(f, g);
        double cs = f / t;
        double sn = g / t;
        if (j != k) e[j - 1] = t;
        f = cs * s[j] + sn * e[j];
        e[j] = cs * e[j] - sn * s[j];
        g = sn * s[j + 1];
        s[j + 1] = cs * s[j + 1];
        for (int i = 0; i < n; ++i) {
          t = cs * v[i][j] + sn * v[i][j + 1];
          v[i][j + 1] = -sn * v[i][j] + cs * v[i][j + 1];
          v[i][j] = t;
        }
        t = stableHypot(f, g);
        cs = f / t;
        sn = g / t;
        s[j] = t;
        f = cs * e[j] + sn * s[j + 1];
        s[j + 1] = -sn * e[j] + cs * s[j + 1];
        g = sn * e[j + 1];
        e[j + 1] = cs * e[j + 1];
        if (j < m - 1) {
          for (int i = 0; i < m; ++i) {
            t = cs * u[i][j] + sn * u[i][j + 1];
            u[i][j + 1] = -sn * u[i][j] + cs * u[i][j + 1];
            u[i][j] = t;
          }
        }
      }
      e[p - 2] = f;
    } else {
      if (s[k] <= 0.0) {
        s[k] = s[k] < 0.0 ? -s[k] : 0.0;
        for (int i = 0; i <= pp; ++i) v[i][k] = -v[i][k];
      }
      while (k < pp) {
        if (s[k] >= s[k + 1]) break;
        std::swap(s[k], s[k + 1]);
        if (k < n - 1)
          for (int i = 0; i < n; ++i) std::swap(v[i][k + 1], v[i][k]);
        if (k < m - 1)
          for (int i = 0; i < m; ++i) std::swap(u[i][k + 1], u[i][k]);
        ++k;
      }
      --p;
    }
  }
  return {std::move(u), std::move(v), std::move(s)};
}

Matrix transpose(const Matrix& value) {
  Matrix result(value.at(0).size(), Vector(value.size(), 0.0));
  for (int i = 0; i < value.size(); ++i)
    for (int j = 0; j < value.at(0).size(); ++j) result[j][i] = value[i][j];
  return result;
}

Matrix multiply(const Matrix& lhs, const Matrix& rhs) {
  Matrix result(lhs.size(), Vector(rhs.at(0).size(), 0.0));
  for (int i = 0; i < lhs.size(); ++i)
    for (int j = 0; j < rhs.at(0).size(); ++j)
      for (int k = 0; k < lhs.at(0).size(); ++k)
        result[i][j] += lhs[i][k] * rhs[k][j];
  return result;
}

double dot(const Vector& lhs, const Vector& rhs) {
  double result = 0.0;
  for (int i = 0; i < lhs.size(); ++i) result += lhs[i] * rhs[i];
  return result;
}

Vector normalized(const Vector& value) {
  const double magnitude = std::sqrt(dot(value, value));
  Vector result(value.size());
  for (int i = 0; i < value.size(); ++i) result[i] = value[i] / magnitude;
  return result;
}

Vector centered(const Vector& value) {
  double sum = 0.0;
  for (double item : value) sum += item;
  sum *= -1.0 / value.size();
  Vector result(value.size());
  for (int i = 0; i < value.size(); ++i) result[i] = sum + value[i];
  return result;
}

Vector multiplyL(const Vector& value, const Matrix& c, const Matrix& inverse) {
  Vector first(c.at(0).size(), 0.0);
  for (int i = 0; i < c.at(0).size(); ++i)
    for (int j = 0; j < c.size(); ++j) first[i] += -0.5 * c[j][i] * value[j];
  Vector second(inverse.size(), 0.0);
  for (int i = 0; i < inverse.size(); ++i)
    for (int j = 0; j < inverse.size(); ++j) second[i] += inverse[i][j] * first[j];
  Vector result(c.size(), 0.0);
  for (int i = 0; i < c.size(); ++i)
    for (int j = 0; j < c.at(0).size(); ++j) result[i] += c[i][j] * second[j];
  return result;
}

Vector spectralProduct(const Vector& value, const Matrix& c,
                       const Matrix& inverse) {
  return centered(multiplyL(centered(value), c, inverse));
}

}  // namespace

QVector<QPointF> layoutArchitectureSpectral(
    const QVector<QVector<int>>& adjacency, qreal nodeSeparation,
    const std::function<double()>& random) {
  const int nodeCount = adjacency.size();
  QVector<QPointF> result(nodeCount);
  if (nodeCount <= 0) return result;
  if (nodeCount == 1) return result;
  if (nodeCount == 2) {
    result[1].setX(nodeSeparation);
    return result;
  }

  const int sampleCount = std::min(nodeCount, 25);
  QVector<int> samples(sampleCount, 0);
  QVector<double> minimum(nodeCount, 100000000.0);
  Matrix c(nodeCount, Vector(sampleCount, 0.0));
  int sample = int(std::floor(random() * nodeCount));
  for (int column = 0; column < sampleCount; ++column) {
    samples[column] = sample;
    QVector<int> distance(nodeCount, 100000000);
    QVector<int> queue;
    queue.reserve(nodeCount);
    queue.append(sample);
    distance[sample] = 0;
    for (int front = 0; front < queue.size(); ++front) {
      const int current = queue.at(front);
      for (int neighbor : adjacency.at(current)) {
        if (neighbor < 0 || neighbor >= nodeCount ||
            distance[neighbor] != 100000000)
          continue;
        distance[neighbor] = distance[current] + 1;
        queue.append(neighbor);
      }
      c[current][column] = distance[current] * nodeSeparation;
    }
    double furthest = 0.0;
    int furthestIndex = 1;
    for (int i = 0; i < nodeCount; ++i) {
      minimum[i] = std::min(minimum[i], c[i][column]);
      if (minimum[i] > furthest) {
        furthest = minimum[i];
        furthestIndex = i;
      }
    }
    sample = furthestIndex;
  }
  for (Vector& row : c)
    for (double& value : row) value *= value;

  Matrix phi(sampleCount, Vector(sampleCount, 0.0));
  for (int i = 0; i < sampleCount; ++i)
    for (int j = 0; j < sampleCount; ++j) phi[i][j] = c[samples[j]][i];
  const SvdResult decomposition = svd(phi);
  const double maximum = decomposition.singular.at(0) *
                         decomposition.singular.at(0) *
                         decomposition.singular.at(0);
  Matrix sigma(sampleCount, Vector(sampleCount, 0.0));
  for (int i = 0; i < sampleCount; ++i) {
    const double q = decomposition.singular.at(i);
    sigma[i][i] = q / (q * q + maximum / (q * q));
  }
  const Matrix inverse = multiply(multiply(decomposition.v, sigma),
                                  transpose(decomposition.u));

  Vector y1(nodeCount), y2(nodeCount);
  for (int i = 0; i < nodeCount; ++i) {
    y1[i] = random();
    y2[i] = random();
  }
  y1 = normalized(y1);
  y2 = normalized(y2);
  Vector v1(nodeCount), v2(nodeCount);
  double theta1 = 0.0;
  double theta2 = 0.0;
  double previous = 1e-9;
  for (;;) {
    v1 = y1;
    y1 = spectralProduct(v1, c, inverse);
    theta1 = dot(v1, y1);
    y1 = normalized(y1);
    const double current = dot(v1, y1);
    const double ratio = std::abs(current / previous);
    if (ratio <= 1.0000001 && ratio >= 1.0) break;
    previous = current;
  }
  v1 = y1;
  previous = 1e-9;
  for (;;) {
    v2 = y2;
    const double projection = dot(v1, v2);
    for (int i = 0; i < nodeCount; ++i) v2[i] -= v1[i] * projection;
    y2 = spectralProduct(v2, c, inverse);
    theta2 = dot(v2, y2);
    y2 = normalized(y2);
    const double current = dot(v2, y2);
    const double ratio = std::abs(current / previous);
    if (ratio <= 1.0000001 && ratio >= 1.0) break;
    previous = current;
  }
  v2 = y2;
  const double scaleX = std::sqrt(std::abs(theta1));
  const double scaleY = std::sqrt(std::abs(theta2));
  for (int i = 0; i < nodeCount; ++i)
    result[i] = QPointF(v1[i] * scaleX, v2[i] * scaleY);
  return result;
}

void transformArchitectureSpectralConstraints(
    QVector<QPointF>& positions,
    const QVector<QVector<int>>& verticalAlignments,
    const QVector<QVector<int>>& horizontalAlignments,
    const QVector<ArchitectureSpectralRelativeConstraint>& relative) {
  Matrix target;
  Matrix source;
  auto appendVertical = [&](const QVector<int>& group) {
    if (group.isEmpty()) return;
    double average = 0.0;
    for (int index : group) average += positions.at(index).x();
    average /= group.size();
    for (int index : group) {
      target.append({average, positions.at(index).y()});
      source.append({positions.at(index).x(), positions.at(index).y()});
    }
  };
  auto appendHorizontal = [&](const QVector<int>& group) {
    if (group.isEmpty()) return;
    double average = 0.0;
    for (int index : group) average += positions.at(index).y();
    average /= group.size();
    for (int index : group) {
      target.append({positions.at(index).x(), average});
      source.append({positions.at(index).x(), positions.at(index).y()});
    }
  };
  for (const QVector<int>& group : verticalAlignments) appendVertical(group);
  for (const QVector<int>& group : horizontalAlignments) appendHorizontal(group);

  if (!source.isEmpty()) {
    Matrix targetTranspose = transpose(target);
    Matrix sourceTranspose = transpose(source);
    for (Vector& row : targetTranspose) row = centered(row);
    for (Vector& row : sourceTranspose) row = centered(row);
    const Matrix product = multiply(targetTranspose, transpose(sourceTranspose));
    const SvdResult decomposition = svd(product);
    const Matrix transform =
        multiply(decomposition.v, transpose(decomposition.u));
    for (QPointF& point : positions) {
      const double x = point.x();
      const double y = point.y();
      point.setX(x * transform[0][0] + y * transform[1][0]);
      point.setY(x * transform[0][1] + y * transform[1][1]);
    }
  }

  if (!source.isEmpty() && !relative.isEmpty()) {
    int reflectHorizontal = 0;
    int keepHorizontal = 0;
    int reflectVertical = 0;
    int keepVertical = 0;
    for (const ArchitectureSpectralRelativeConstraint& constraint : relative) {
      if (constraint.before < 0 || constraint.after < 0 ||
          constraint.before >= positions.size() ||
          constraint.after >= positions.size())
        continue;
      if (constraint.horizontal) {
        if (positions.at(constraint.before).x() -
                positions.at(constraint.after).x() >=
            0.0)
          ++reflectHorizontal;
        else
          ++keepHorizontal;
      } else {
        if (positions.at(constraint.before).y() -
                positions.at(constraint.after).y() >=
            0.0)
          ++reflectVertical;
        else
          ++keepVertical;
      }
    }
    const bool flipX = reflectHorizontal > keepHorizontal;
    const bool flipY = reflectVertical > keepVertical;
    for (QPointF& point : positions) {
      if (flipX) point.setX(-point.x());
      if (flipY) point.setY(-point.y());
    }
  }
}

}  // namespace muffin::mermaid::architecture
