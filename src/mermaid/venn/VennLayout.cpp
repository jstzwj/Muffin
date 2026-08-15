#include "mermaid/venn/VennLayout.h"

#include "mermaid/editor/MermaidRenderSupport.h"

#include <QHash>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <utility>

// This file is an independent C++ port of the MIT-licensed
// @upsetjs/venn.js 2.0.0 layout, circle-intersection, and fmin routines used
// by Mermaid 11.16.0. Upstream sources:
//   src/layout.js, src/circleintersection.js, src/diagram.js
//   build/venn.esm.js (bundled fmin 0.0.4 routines)

namespace muffin::mermaid::venn::layout {
namespace {

constexpr double kSmall = 1e-10;
constexpr double kPi = 3.141592653589793238462643383279502884;

struct AreaStats {
  double area = 0.0;
  double arcArea = 0.0;
  double polygonArea = 0.0;
  QVector<Arc> arcs;
};

struct IndexedPoint : QPointF {
  QVector<int> parents;
  double angle = 0.0;
};

double distance(const QPointF& left, const QPointF& right) {
  const double dx = left.x() - right.x();
  const double dy = left.y() - right.y();
  return std::sqrt(dx * dx + dy * dy);
}

double distance(const Circle& left, const Circle& right) {
  return distance(QPointF(left.x, left.y), QPointF(right.x, right.y));
}

double distance(const Circle& circle, const QPointF& point) {
  return distance(QPointF(circle.x, circle.y), point);
}

double circleArea(double radius, double width) {
  return radius * radius * std::acos(1.0 - width / radius) -
         (radius - width) * std::sqrt(width * (2.0 * radius - width));
}

double circleOverlap(double leftRadius, double rightRadius, double separation) {
  if (separation >= leftRadius + rightRadius) return 0.0;
  if (separation <= std::abs(leftRadius - rightRadius)) {
    const double radius = std::min(leftRadius, rightRadius);
    return kPi * radius * radius;
  }
  const double leftWidth =
      leftRadius -
      (separation * separation - rightRadius * rightRadius +
       leftRadius * leftRadius) /
          (2.0 * separation);
  const double rightWidth =
      rightRadius -
      (separation * separation - leftRadius * leftRadius +
       rightRadius * rightRadius) /
          (2.0 * separation);
  return circleArea(leftRadius, leftWidth) +
         circleArea(rightRadius, rightWidth);
}

QVector<QPointF> circleCircleIntersection(const Circle& left,
                                          const Circle& right) {
  const double d = distance(left, right);
  if (d >= left.radius + right.radius ||
      d <= std::abs(left.radius - right.radius))
    return {};
  const double a = (left.radius * left.radius - right.radius * right.radius +
                    d * d) /
                   (2.0 * d);
  const double h = std::sqrt(left.radius * left.radius - a * a);
  const double x0 = left.x + a * (right.x - left.x) / d;
  const double y0 = left.y + a * (right.y - left.y) / d;
  const double rx = -(right.y - left.y) * h / d;
  const double ry = -(right.x - left.x) * h / d;
  return {{x0 + rx, y0 - ry}, {x0 - rx, y0 + ry}};
}

bool contained(const QPointF& point, const QVector<Circle>& circles) {
  return std::all_of(circles.cbegin(), circles.cend(), [&](const Circle& circle) {
    return distance(circle, point) < circle.radius + kSmall;
  });
}

QPointF centerOf(const QVector<IndexedPoint>& points) {
  QPointF center;
  for (const auto& point : points) center += point;
  return center / points.size();
}

double intersectionAreaImpl(const QVector<Circle>& circles, AreaStats* stats) {
  QVector<IndexedPoint> intersections;
  for (int i = 0; i < circles.size(); ++i) {
    for (int j = i + 1; j < circles.size(); ++j) {
      for (const QPointF& point : circleCircleIntersection(circles.at(i), circles.at(j))) {
        IndexedPoint indexed;
        indexed.setX(point.x());
        indexed.setY(point.y());
        indexed.parents = {i, j};
        intersections.append(indexed);
      }
    }
  }

  QVector<IndexedPoint> inner;
  for (const auto& point : intersections)
    if (contained(point, circles)) inner.append(point);

  double arcArea = 0.0;
  double polygonArea = 0.0;
  QVector<Arc> arcs;
  if (inner.size() > 1) {
    const QPointF center = centerOf(inner);
    for (auto& point : inner)
      point.angle = std::atan2(point.x() - center.x(), point.y() - center.y());
    std::stable_sort(inner.begin(), inner.end(),
                     [](const IndexedPoint& left, const IndexedPoint& right) {
                       return left.angle > right.angle;
                     });

    IndexedPoint p2 = inner.back();
    for (const IndexedPoint& p1 : inner) {
      polygonArea += (p2.x() + p1.x()) * (p1.y() - p2.y());
      const QPointF midpoint = (p1 + p2) / 2.0;
      std::optional<Arc> best;
      for (int parent : p1.parents) {
        if (!p2.parents.contains(parent)) continue;
        const Circle& circle = circles.at(parent);
        const double a1 = std::atan2(p1.x() - circle.x, p1.y() - circle.y);
        const double a2 = std::atan2(p2.x() - circle.x, p2.y() - circle.y);
        double angleDifference = a2 - a1;
        if (angleDifference < 0.0) angleDifference += 2.0 * kPi;
        const double angle = a2 - angleDifference / 2.0;
        double width = distance(
            midpoint, QPointF(circle.x + circle.radius * std::sin(angle),
                              circle.y + circle.radius * std::cos(angle)));
        width = std::min(width, circle.radius * 2.0);
        if (!best || best->width > width) {
          best = Arc{circle, width, p1, p2, width > circle.radius, true};
        }
      }
      if (best) {
        arcs.append(*best);
        arcArea += circleArea(best->circle.radius, best->width);
        p2 = p1;
      }
    }
  } else if (!circles.isEmpty()) {
    const Circle* smallest = &circles.front();
    for (const Circle& circle : circles)
      if (circle.radius < smallest->radius) smallest = &circle;
    bool disjoint = false;
    for (const Circle& circle : circles) {
      if (distance(circle, *smallest) >
          std::abs(smallest->radius - circle.radius)) {
        disjoint = true;
        break;
      }
    }
    if (!disjoint) {
      arcArea = smallest->radius * smallest->radius * kPi;
      arcs.append(Arc{*smallest,
                      smallest->radius * 2.0,
                      QPointF(smallest->x,
                              smallest->y + smallest->radius),
                      QPointF(smallest->x - kSmall,
                              smallest->y + smallest->radius),
                      true,
                      true});
    }
  }
  polygonArea /= 2.0;
  if (stats) {
    stats->area = arcArea + polygonArea;
    stats->arcArea = arcArea;
    stats->polygonArea = polygonArea;
    stats->arcs = arcs;
  }
  return arcArea + polygonArea;
}

double bisect(const std::function<double(double)>& function, double a,
              double b) {
  const double fa = function(a);
  const double fb = function(b);
  double delta = b - a;
  if (fa * fb > 0.0) throw std::runtime_error("Initial bisect points must have opposite signs");
  if (fa == 0.0) return a;
  if (fb == 0.0) return b;
  for (int iteration = 0; iteration < 100; ++iteration) {
    delta /= 2.0;
    const double mid = a + delta;
    const double fm = function(mid);
    if (fm * fa >= 0.0) a = mid;
    if (std::abs(delta) < 1e-10 || fm == 0.0) return mid;
  }
  return a + delta;
}

double distanceFromIntersectArea(double leftRadius, double rightRadius,
                                 double overlap) {
  const double radius = std::min(leftRadius, rightRadius);
  if (radius * radius * kPi <= overlap + kSmall)
    return std::abs(leftRadius - rightRadius);
  return bisect(
      [&](double d) { return circleOverlap(leftRadius, rightRadius, d) - overlap; },
      0.0, leftRadius + rightRadius);
}

double dot(const QVector<double>& left, const QVector<double>& right) {
  double result = 0.0;
  for (int i = 0; i < left.size(); ++i) result += left.at(i) * right.at(i);
  return result;
}

void weightedSum(QVector<double>& output, double leftWeight,
                 const QVector<double>& left, double rightWeight,
                 const QVector<double>& right) {
  for (int i = 0; i < output.size(); ++i)
    output[i] = leftWeight * left.at(i) + rightWeight * right.at(i);
}

struct SimplexPoint {
  QVector<double> x;
  double fx = 0.0;
  int id = 0;
};

SimplexPoint nelderMead(const std::function<double(const QVector<double>&)>& function,
                        const QVector<double>& initial,
                        int maxIterations = -1,
                        double minErrorDelta = 1e-6,
                        double minTolerance = 1e-5) {
  const int count = initial.size();
  if (maxIterations < 0) maxIterations = count * 200;
  QVector<SimplexPoint> simplex(count + 1);
  simplex[0] = {initial, function(initial), 0};
  for (int i = 0; i < count; ++i) {
    QVector<double> point = initial;
    point[i] = point.at(i) != 0.0 ? point.at(i) * 1.05 : 0.001;
    simplex[i + 1] = {point, function(point), i + 1};
  }
  QVector<double> centroid = initial;
  QVector<double> reflected = initial;
  QVector<double> contracted = initial;
  QVector<double> expanded = initial;
  auto order = [](const SimplexPoint& left, const SimplexPoint& right) {
    return left.fx < right.fx;
  };
  const auto replaceWorst = [&](const QVector<double>& value, double fx) {
    simplex[count].x = value;
    simplex[count].fx = fx;
  };

  for (int iteration = 0; iteration < maxIterations; ++iteration) {
    std::stable_sort(simplex.begin(), simplex.end(), order);
    double maxDifference = 0.0;
    for (int i = 0; i < count; ++i)
      maxDifference = std::max(
          maxDifference, std::abs(simplex.at(0).x.at(i) - simplex.at(1).x.at(i)));
    if (std::abs(simplex.front().fx - simplex.back().fx) < minErrorDelta &&
        maxDifference < minTolerance)
      break;

    for (int i = 0; i < count; ++i) {
      centroid[i] = 0.0;
      for (int j = 0; j < count; ++j) centroid[i] += simplex.at(j).x.at(i);
      centroid[i] /= count;
    }
    const SimplexPoint& worst = simplex.back();
    weightedSum(reflected, 2.0, centroid, -1.0, worst.x);
    const double reflectedFx = function(reflected);
    if (reflectedFx < simplex.front().fx) {
      weightedSum(expanded, 3.0, centroid, -2.0, worst.x);
      const double expandedFx = function(expanded);
      if (expandedFx < reflectedFx)
        replaceWorst(expanded, expandedFx);
      else
        replaceWorst(reflected, reflectedFx);
    } else if (reflectedFx >= simplex.at(count - 1).fx) {
      bool reduce = false;
      if (reflectedFx > worst.fx) {
        weightedSum(contracted, 0.5, centroid, 0.5, worst.x);
        const double fx = function(contracted);
        if (fx < worst.fx)
          replaceWorst(contracted, fx);
        else
          reduce = true;
      } else {
        weightedSum(contracted, 1.5, centroid, -0.5, worst.x);
        const double fx = function(contracted);
        if (fx < reflectedFx)
          replaceWorst(contracted, fx);
        else
          reduce = true;
      }
      if (reduce) {
        for (int i = 1; i < simplex.size(); ++i) {
          weightedSum(simplex[i].x, 0.5, simplex.front().x, 0.5, simplex.at(i).x);
          simplex[i].fx = function(simplex.at(i).x);
        }
      }
    } else {
      replaceWorst(reflected, reflectedFx);
    }
  }
  std::stable_sort(simplex.begin(), simplex.end(), order);
  return simplex.front();
}

QString keyFor(const QStringList& sets, QChar separator = QLatin1Char('|')) {
  return sets.join(separator);
}

struct CircleSet {
  QVector<Circle> circles;
  QHash<QString, int> indexes;

  Circle& byId(const QString& id) { return circles[indexes.value(id)]; }
  const Circle& byId(const QString& id) const { return circles.at(indexes.value(id)); }
};

QVector<VennSubset> addMissingAreas(const QVector<VennSubset>& input) {
  QVector<VennSubset> result = input;
  QStringList ids;
  QSet<QString> pairs;
  for (const VennSubset& area : result) {
    if (area.sets.size() == 1)
      ids.append(area.sets.front());
    else if (area.sets.size() == 2) {
      pairs.insert(area.sets.join(QLatin1Char(';')));
      pairs.insert(QStringList{area.sets.at(1), area.sets.at(0)}.join(QLatin1Char(';')));
    }
  }
  std::sort(ids.begin(), ids.end());
  for (int i = 0; i < ids.size(); ++i) {
    for (int j = i + 1; j < ids.size(); ++j) {
      const QString key = QStringList{ids.at(i), ids.at(j)}.join(QLatin1Char(';'));
      if (!pairs.contains(key))
        result.append(VennSubset{{ids.at(i), ids.at(j)}, 0.0, QString(), false});
    }
  }
  return result;
}

double lossFunction(const CircleSet& circles,
                    const QVector<VennSubset>& overlaps) {
  double loss = 0.0;
  for (const VennSubset& overlap : overlaps) {
    if (overlap.sets.size() == 1) continue;
    double actual = 0.0;
    if (overlap.sets.size() == 2) {
      const Circle& left = circles.byId(overlap.sets.at(0));
      const Circle& right = circles.byId(overlap.sets.at(1));
      actual = circleOverlap(left.radius, right.radius, distance(left, right));
    } else {
      QVector<Circle> selected;
      for (const QString& id : overlap.sets) selected.append(circles.byId(id));
      actual = intersectionAreaImpl(selected, nullptr);
    }
    const double delta = actual - overlap.size;
    loss += delta * delta;
  }
  return loss;
}

CircleSet greedyLayout(const QVector<VennSubset>& areas) {
  CircleSet circles;
  struct Overlap { QString set; double size = 0.0; double weight = 1.0; };
  QHash<QString, QVector<Overlap>> overlaps;
  for (const VennSubset& area : areas) {
    if (area.sets.size() != 1) continue;
    const QString id = area.sets.front();
    circles.indexes.insert(id, circles.circles.size());
    circles.circles.append(Circle{id, 1e10, 1e10, std::sqrt(area.size / kPi)});
    overlaps.insert(id, {});
  }
  for (const VennSubset& area : areas) {
    if (area.sets.size() != 2) continue;
    const QString left = area.sets.at(0);
    const QString right = area.sets.at(1);
    double weight = 1.0;
    const Circle& lc = circles.byId(left);
    const Circle& rc = circles.byId(right);
    const double leftSize = lc.radius * lc.radius * kPi;
    const double rightSize = rc.radius * rc.radius * kPi;
    if (area.size + kSmall >= std::min(leftSize, rightSize)) weight = 0.0;
    overlaps[left].append({right, area.size, weight});
    overlaps[right].append({left, area.size, weight});
  }

  struct MostOverlap { QString set; double size = 0.0; };
  QVector<MostOverlap> order;
  for (const Circle& circle : circles.circles) {
    double total = 0.0;
    for (const Overlap& overlap : overlaps.value(circle.set))
      total += overlap.size * overlap.weight;
    order.append({circle.set, total});
  }
  std::stable_sort(order.begin(), order.end(),
                   [](const MostOverlap& left, const MostOverlap& right) {
                     return right.size < left.size;
                   });
  if (order.isEmpty()) return circles;
  QSet<QString> positioned;
  circles.byId(order.front().set).x = 0.0;
  circles.byId(order.front().set).y = 0.0;
  positioned.insert(order.front().set);

  for (int index = 1; index < order.size(); ++index) {
    const QString id = order.at(index).set;
    Circle& current = circles.byId(id);
    QVector<Overlap> placed;
    for (const Overlap& overlap : overlaps.value(id))
      if (positioned.contains(overlap.set)) placed.append(overlap);
    std::stable_sort(placed.begin(), placed.end(),
                     [](const Overlap& left, const Overlap& right) {
                       return right.size < left.size;
                     });
    if (placed.isEmpty()) throw std::runtime_error("ERROR: missing pairwise overlap information");
    QVector<QPointF> candidates;
    for (int j = 0; j < placed.size(); ++j) {
      const Circle& other = circles.byId(placed.at(j).set);
      const double d = distanceFromIntersectArea(current.radius, other.radius,
                                                 placed.at(j).size);
      candidates += {QPointF(other.x + d, other.y), QPointF(other.x - d, other.y),
                     QPointF(other.x, other.y + d), QPointF(other.x, other.y - d)};
      for (int k = j + 1; k < placed.size(); ++k) {
        const Circle& second = circles.byId(placed.at(k).set);
        const double secondDistance = distanceFromIntersectArea(
            current.radius, second.radius, placed.at(k).size);
        Circle firstDistance{QString(), other.x, other.y, d};
        Circle secondDistanceCircle{QString(), second.x, second.y, secondDistance};
        candidates += circleCircleIntersection(firstDistance, secondDistanceCircle);
      }
    }
    double bestLoss = 1e50;
    QPointF best = candidates.front();
    for (const QPointF& candidate : candidates) {
      current.x = candidate.x();
      current.y = candidate.y();
      const double candidateLoss = lossFunction(circles, areas);
      if (candidateLoss < bestLoss) {
        bestLoss = candidateLoss;
        best = candidate;
      }
    }
    current.x = best.x();
    current.y = best.y();
    positioned.insert(id);
  }
  return circles;
}

CircleSet optimizeLayout(const QVector<VennSubset>& original) {
  const QVector<VennSubset> areas = addMissingAreas(original);
  CircleSet circles = greedyLayout(areas);
  QVector<double> initial;
  for (const Circle& circle : circles.circles) {
    initial.append(circle.x);
    initial.append(circle.y);
  }
  const SimplexPoint solution = nelderMead(
      [&](const QVector<double>& values) {
        CircleSet current = circles;
        for (int i = 0; i < current.circles.size(); ++i) {
          current.circles[i].x = values.at(2 * i);
          current.circles[i].y = values.at(2 * i + 1);
        }
        return lossFunction(current, areas);
      },
      initial, 500);
  for (int i = 0; i < circles.circles.size(); ++i) {
    circles.circles[i].x = solution.x.at(2 * i);
    circles.circles[i].y = solution.x.at(2 * i + 1);
  }
  return circles;
}

struct Bounds {
  double minX = std::numeric_limits<double>::infinity();
  double maxX = -std::numeric_limits<double>::infinity();
  double minY = std::numeric_limits<double>::infinity();
  double maxY = -std::numeric_limits<double>::infinity();
};

Bounds boundsOf(const QVector<Circle>& circles) {
  Bounds bounds;
  for (const Circle& circle : circles) {
    bounds.minX = std::min(bounds.minX, circle.x - circle.radius);
    bounds.maxX = std::max(bounds.maxX, circle.x + circle.radius);
    bounds.minY = std::min(bounds.minY, circle.y - circle.radius);
    bounds.maxY = std::max(bounds.maxY, circle.y + circle.radius);
  }
  return bounds;
}

void orientate(QVector<Circle*>& circles, double orientation) {
  std::stable_sort(circles.begin(), circles.end(),
                   [](const Circle* left, const Circle* right) {
                     return right->radius < left->radius;
                   });
  if (!circles.isEmpty()) {
    const double x = circles.front()->x;
    const double y = circles.front()->y;
    for (Circle* circle : circles) {
      circle->x -= x;
      circle->y -= y;
    }
  }
  if (circles.size() == 2 &&
      distance(*circles.at(0), *circles.at(1)) <
          std::abs(circles.at(1)->radius - circles.at(0)->radius)) {
    circles[1]->x = circles.at(0)->x + circles.at(0)->radius -
                    circles.at(1)->radius - 1e-10;
    circles[1]->y = circles.at(0)->y;
  }
  if (circles.size() > 1) {
    const double rotation =
        std::atan2(circles.at(1)->x, circles.at(1)->y) - orientation;
    const double cosine = std::cos(rotation);
    const double sine = std::sin(rotation);
    for (Circle* circle : circles) {
      const double x = circle->x;
      const double y = circle->y;
      circle->x = cosine * x - sine * y;
      circle->y = sine * x + cosine * y;
    }
  }
  if (circles.size() > 2) {
    double angle = std::atan2(circles.at(2)->x, circles.at(2)->y) - orientation;
    while (angle < 0.0) angle += 2.0 * kPi;
    while (angle > 2.0 * kPi) angle -= 2.0 * kPi;
    if (angle > kPi) {
      const double slope = circles.at(1)->y / (1e-10 + circles.at(1)->x);
      for (Circle* circle : circles) {
        const double d = (circle->x + slope * circle->y) /
                         (1.0 + slope * slope);
        circle->x = 2.0 * d - circle->x;
        circle->y = 2.0 * d * slope - circle->y;
      }
    }
  }
}

CircleSet normalize(CircleSet solution) {
  const int count = solution.circles.size();
  if (count == 0) return solution;
  QVector<int> parent(count);
  std::iota(parent.begin(), parent.end(), 0);
  const std::function<int(int)> find = [&](int index) {
    if (parent[index] != index) parent[index] = find(parent[index]);
    return parent[index];
  };
  for (int i = 0; i < count; ++i)
    for (int j = i + 1; j < count; ++j)
      if (distance(solution.circles.at(i), solution.circles.at(j)) + kSmall <
          solution.circles.at(i).radius + solution.circles.at(j).radius)
        parent[find(j)] = find(i);

  QVector<QVector<int>> clusterIndexes;
  QHash<int, int> clusterByRoot;
  for (int i = 0; i < count; ++i) {
    const int root = find(i);
    if (!clusterByRoot.contains(root)) {
      clusterByRoot.insert(root, clusterIndexes.size());
      clusterIndexes.append(QVector<int>{});
    }
    clusterIndexes[clusterByRoot.value(root)].append(i);
  }
  struct Cluster { QVector<int> indexes; Bounds bounds; double size = 0.0; };
  QVector<Cluster> clusters;
  for (const QVector<int>& indexes : clusterIndexes) {
    QVector<Circle*> selected;
    for (int index : indexes) selected.append(&solution.circles[index]);
    orientate(selected, kPi / 2.0);
    QVector<Circle> values;
    for (Circle* circle : selected) values.append(*circle);
    const Bounds bounds = boundsOf(values);
    clusters.append({indexes, bounds,
                     (bounds.maxX - bounds.minX) * (bounds.maxY - bounds.minY)});
  }
  std::stable_sort(clusters.begin(), clusters.end(),
                   [](const Cluster& left, const Cluster& right) {
                     return right.size < left.size;
                   });

  QVector<Circle> arranged;
  for (int index : clusters.front().indexes) arranged.append(solution.circles.at(index));
  Bounds combined = boundsOf(arranged);
  const double spacing = (combined.maxX - combined.minX) / 50.0;
  const auto addCluster = [&](const Cluster* cluster, bool right, bool bottom) {
    if (!cluster) return;
    const Bounds& bounds = cluster->bounds;
    double xOffset;
    double yOffset;
    if (right)
      xOffset = combined.maxX - bounds.minX + spacing;
    else {
      xOffset = combined.maxX - bounds.maxX;
      const double centering = (bounds.maxX - bounds.minX) / 2.0 -
                               (combined.maxX - combined.minX) / 2.0;
      if (centering < 0.0) xOffset += centering;
    }
    if (bottom)
      yOffset = combined.maxY - bounds.minY + spacing;
    else {
      yOffset = combined.maxY - bounds.maxY;
      const double centering = (bounds.maxY - bounds.minY) / 2.0 -
                               (combined.maxY - combined.minY) / 2.0;
      if (centering < 0.0) yOffset += centering;
    }
    for (int index : cluster->indexes) {
      Circle circle = solution.circles.at(index);
      circle.x += xOffset;
      circle.y += yOffset;
      arranged.append(circle);
    }
  };
  for (int i = 1; i < clusters.size(); i += 3) {
    addCluster(&clusters.at(i), true, false);
    addCluster(i + 1 < clusters.size() ? &clusters.at(i + 1) : nullptr,
               false, true);
    addCluster(i + 2 < clusters.size() ? &clusters.at(i + 2) : nullptr,
               true, true);
    combined = boundsOf(arranged);
  }
  solution.circles = arranged;
  solution.indexes.clear();
  for (int i = 0; i < arranged.size(); ++i)
    solution.indexes.insert(arranged.at(i).set, i);
  return solution;
}

CircleSet scale(CircleSet solution, double width, double height,
                const QJsonValue& rawPadding) {
  const double padding = editor::jsNumberValue(rawPadding);
  width -= 2.0 * padding;
  height -= 2.0 * padding;
  const Bounds bounds = boundsOf(solution.circles);
  if (bounds.maxX == bounds.minX || bounds.maxY == bounds.minY) return solution;
  const double xScale = width / (bounds.maxX - bounds.minX);
  const double yScale = height / (bounds.maxY - bounds.minY);
  const double scaling = std::min(yScale, xScale);
  const double xOffset =
      (width - (bounds.maxX - bounds.minX) * scaling) / 2.0;
  const double yOffset =
      (height - (bounds.maxY - bounds.minY) * scaling) / 2.0;
  for (Circle& circle : solution.circles) {
    circle.radius *= scaling;
    const double xTerm = (circle.x - bounds.minX) * scaling;
    const double yTerm = (circle.y - bounds.minY) * scaling;
    if (rawPadding.isString()) {
      // scaleSolution uses JS `+` left-to-right. A string padding first
      // coerces the width subtraction numerically, then concatenates both
      // offsets; later geometry converts the stored coordinate back to Number.
      circle.x = editor::jsNumberValue(QJsonValue(
          rawPadding.toString() + editor::jsNumberToString(xOffset) +
          editor::jsNumberToString(xTerm)));
      circle.y = editor::jsNumberValue(QJsonValue(
          rawPadding.toString() + editor::jsNumberToString(yOffset) +
          editor::jsNumberToString(yTerm)));
    } else {
      circle.x = padding + xOffset + xTerm;
      circle.y = padding + yOffset + yTerm;
    }
  }
  return solution;
}

double circleMargin(const QPointF& current, const QVector<Circle>& interior,
                    const QVector<Circle>& exterior) {
  double margin = interior.front().radius - distance(interior.front(), current);
  for (int i = 1; i < interior.size(); ++i)
    margin = std::min(margin,
                      interior.at(i).radius - distance(interior.at(i), current));
  for (const Circle& circle : exterior)
    margin = std::min(margin, distance(circle, current) - circle.radius);
  return margin;
}

QPair<QPointF, bool> computeTextCentre(const QVector<Circle>& interior,
                                      const QVector<Circle>& exterior) {
  QVector<QPointF> points;
  for (const Circle& circle : interior) {
    points += {QPointF(circle.x, circle.y),
               QPointF(circle.x + circle.radius / 2.0, circle.y),
               QPointF(circle.x - circle.radius / 2.0, circle.y),
               QPointF(circle.x, circle.y + circle.radius / 2.0),
               QPointF(circle.x, circle.y - circle.radius / 2.0)};
  }
  QPointF initial = points.front();
  double margin = circleMargin(initial, interior, exterior);
  for (int i = 1; i < points.size(); ++i) {
    const double current = circleMargin(points.at(i), interior, exterior);
    if (current >= margin) {
      initial = points.at(i);
      margin = current;
    }
  }
  const SimplexPoint optimized = nelderMead(
      [&](const QVector<double>& value) {
        return -circleMargin(QPointF(value.at(0), value.at(1)), interior,
                             exterior);
      },
      {initial.x(), initial.y()}, 500, 1e-10, 1e-10);
  QPointF result(optimized.x.at(0), optimized.x.at(1));
  bool valid = true;
  for (const Circle& circle : interior)
    if (distance(circle, result) > circle.radius) valid = false;
  for (const Circle& circle : exterior)
    if (distance(circle, result) < circle.radius) valid = false;
  if (valid) return {result, false};
  if (interior.size() == 1)
    return {QPointF(interior.front().x, interior.front().y), false};
  AreaStats stats;
  intersectionAreaImpl(interior, &stats);
  if (stats.arcs.isEmpty()) return {QPointF(0.0, -1000.0), true};
  if (stats.arcs.size() == 1)
    return {QPointF(stats.arcs.front().circle.x,
                    stats.arcs.front().circle.y),
            false};
  if (!exterior.isEmpty()) return computeTextCentre(interior, {});
  QPointF center;
  for (const Arc& arc : stats.arcs) center += arc.p1;
  return {center / stats.arcs.size(), false};
}

QString number(double value, int roundDigits) {
  if (roundDigits >= 0) {
    const double factor = std::pow(10.0, roundDigits);
    value = std::round(value * factor) / factor;
  }
  return editor::jsNumberToString(value);
}

QString circlePath(const Circle& circle, int roundDigits) {
  const QString x = number(circle.x, roundDigits);
  const QString y = number(circle.y, roundDigits);
  const QString radius = number(circle.radius, roundDigits);
  const QString negativeRadius = number(-circle.radius, roundDigits);
  const QString diameter = number(circle.radius * 2.0, roundDigits);
  const QString negativeDiameter = number(-circle.radius * 2.0, roundDigits);
  return QStringLiteral("\nM %1 %2 \nm %3 0 \na %4 %4 0 1 0 %5 0 \na %4 %4 0 1 0 %6 0")
      .arg(x, y, negativeRadius, radius, diameter, negativeDiameter);
}

QString arcsPath(const QVector<Arc>& arcs, int roundDigits) {
  if (arcs.isEmpty()) return QStringLiteral("M 0 0");
  if (arcs.size() == 1) return circlePath(arcs.front().circle, roundDigits);
  QString result = QStringLiteral("\nM %1 %2")
                       .arg(number(arcs.front().p2.x(), roundDigits),
                            number(arcs.front().p2.y(), roundDigits));
  for (const Arc& arc : arcs) {
    const QString radius = number(arc.circle.radius, roundDigits);
    result += QStringLiteral(" \nA %1 %1 0 %2 %3 %4 %5")
                  .arg(radius)
                  .arg(arc.large ? 1 : 0)
                  .arg(arc.sweep ? 1 : 0)
                  .arg(number(arc.p1.x(), roundDigits),
                       number(arc.p1.y(), roundDigits));
  }
  return result;
}

}  // namespace

double intersectionArea(const QVector<Circle>& circles, QVector<Arc>* arcs) {
  AreaStats stats;
  const double area = intersectionAreaImpl(circles, arcs ? &stats : nullptr);
  if (arcs) *arcs = stats.arcs;
  return area;
}

QString intersectionPath(const QVector<Circle>& circles, int roundDigits) {
  AreaStats stats;
  intersectionAreaImpl(circles, &stats);
  return arcsPath(stats.arcs, roundDigits);
}

Result compute(const QVector<VennSubset>& data, double width, double height,
               const QJsonValue& padding) {
  Result result;
  if (data.isEmpty()) return result;
  CircleSet circles = scale(normalize(optimizeLayout(data)), width, height, padding);
  result.circles = circles.circles;

  QHash<QString, QStringList> overlapping;
  for (const Circle& circle : circles.circles) overlapping.insert(circle.set, {});
  for (int i = 0; i < circles.circles.size(); ++i) {
    const Circle& left = circles.circles.at(i);
    for (int j = i + 1; j < circles.circles.size(); ++j) {
      const Circle& right = circles.circles.at(j);
      const double d = distance(left, right);
      if (d + right.radius <= left.radius + kSmall)
        overlapping[right.set].append(left.set);
      else if (d + left.radius <= right.radius + kSmall)
        overlapping[left.set].append(right.set);
    }
  }

  for (const VennSubset& subset : data) {
    Area area;
    area.data = subset;
    QSet<QString> ids;
    QSet<QString> excluded;
    for (const QString& id : subset.sets) {
      ids.insert(id);
      for (const QString& overlap : overlapping.value(id)) excluded.insert(overlap);
      area.circles.append(circles.byId(id));
    }
    QVector<Circle> exterior;
    for (const Circle& circle : circles.circles)
      if (!ids.contains(circle.set) && !excluded.contains(circle.set))
        exterior.append(circle);
    const auto text = computeTextCentre(area.circles, exterior);
    area.text = text.first;
    area.textDisjoint = text.second;
    intersectionAreaImpl(area.circles, nullptr);
    area.path = intersectionPath(area.circles, 2);
    area.distinctPath = area.path;
    result.areas.append(std::move(area));
  }
  for (Area& area : result.areas) {
    for (const Area& larger : std::as_const(result.areas)) {
      if (larger.data.sets.size() <= area.data.sets.size()) continue;
      bool containsAll = true;
      for (const QString& id : area.data.sets)
        if (!larger.data.sets.contains(id)) containsAll = false;
      if (containsAll) area.distinctPath += QLatin1Char(' ') + larger.path;
    }
    intersectionArea(area.circles, &area.arcs);
  }
  return result;
}

Result compute(const QVector<VennSubset>& data, double width, double height,
               double padding) {
  return compute(data, width, height, QJsonValue(padding));
}

}  // namespace muffin::mermaid::venn::layout
