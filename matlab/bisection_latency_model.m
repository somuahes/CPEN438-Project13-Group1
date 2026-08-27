%% bisection_latency_model.m
%  Project 13 -- Wiring the Data Centre: Interconnection Networks for a
%  Simulated Accra Warehouse-Scale Cluster.
%  CPEN 438, Group 1. Week 3 deliverable (MATLAB analytical model).
%
%  PURPOSE
%  -------
%  Predict, from topology structure alone, how each network should behave
%  under load, and validate those predictions against the flit-level
%  simulator's measurements (results/week3_sweep_results.csv).
%
%  The model computes, for each (topology, traffic pattern) pair:
%
%    H      average hop count, weighted by the actual destination
%           distribution and the actual routing algorithm
%    gamma  the load, in units of the per-node injection rate, carried by
%           the single most heavily loaded unidirectional channel
%    lam*   the ideal saturation injection rate, 1/gamma, capped by the
%           ejection bandwidth of the busiest destination
%    T0     zero-load latency = H router hops + (L-1) serialisation cycles
%    T(lam) T0 + (rho/(1-rho)) * L/2,  rho = lam/lam*   (M/D/1 source queue)
%
%  Note that gamma is computed by actually walking every source-destination
%  pair through the routing function, not by assuming a uniform cut. That
%  matters: under hot-node traffic the binding constraint is NOT the
%  textbook bisection cut but the handful of channels feeding the database
%  nodes, and only an explicit channel-load calculation exposes that. The
%  classical bisection bound is still reported alongside it for comparison,
%  because it is the bound the Project 13 worked example uses.
%
%  USAGE
%  -----
%    >> bisection_latency_model                    % uses ../results/
%    >> bisection_latency_model('../results')
%
%  OUTPUTS
%  -------
%    figure 1  latency vs injection rate, model vs measurement, all four
%              baseline configurations
%    figure 2  bar chart of ideal vs achieved saturation rate
%    ../results/week3_model_vs_measured.csv
%
%  Tested with MATLAB R2023b; the file is also GNU Octave compatible.

function bisection_latency_model(resultsDir)

if nargin < 1
    resultsDir = fullfile(fileparts(mfilename('fullpath')), '..', 'results');
end

%% ---- Group 1 configuration -------------------------------------------
N            = 16;      % nodes
ROWS         = 4;       % mesh rows
COLS         = 4;       % mesh columns
L            = 4;       % flits per packet
EJECT_BW     = 4;       % flits a node can eject per cycle
HOT          = [5 1];   % hot database nodes, derived from seed 1301
HOT_FRACTION = 0.50;    % share of all packets addressed to them
ROUTER_DELAY = 1;       % cycles per hop

configs = { 'ring', 'uniform'; ...
            'mesh', 'uniform'; ...
            'ring', 'hotnode'; ...
            'mesh', 'hotnode' };

%% ---- Static topology metrics (must agree with Week 2) ----------------
ringDiameter    = floor(N/2);                 % = 8
ringBisection   = 2;                          % links
meshDiameter    = (ROWS-1) + (COLS-1);        % = 6
meshBisection   = min(ROWS, COLS);            % = 4 links

fprintf('\n=== Week 2 static metrics used by the model ===\n');
fprintf('  ring(%d) : diameter %d, bisection %d links\n', N, ringDiameter, ringBisection);
fprintf('  mesh(%dx%d): diameter %d, bisection %d links\n\n', ROWS, COLS, meshDiameter, meshBisection);

%% ---- Analytical model per configuration ------------------------------
model = struct([]);
for k = 1:size(configs,1)
    topo = configs{k,1};
    pat  = configs{k,2};

    P = destinationDistribution(N, pat, HOT, HOT_FRACTION);
    [H, gamma, ejectLoad] = channelLoad(N, ROWS, COLS, topo, P);

    lamChannel = 1 / gamma;                 % channel-limited saturation
    lamEject   = EJECT_BW / max(ejectLoad); % ejection-limited saturation
    lamStar    = min(lamChannel, lamEject);

    % Classical bisection bound, for comparison with the worked example.
    if strcmp(topo,'ring'), B = ringBisection; else, B = meshBisection; end
    crossFraction = (N/2) * ((N/2)/(N-1));  % flits/cycle crossing per lambda
    lamBisection  = B / crossFraction;

    T0 = H * ROUTER_DELAY + (L - 1);        % zero-load latency

    model(k).topology   = topo;
    model(k).traffic    = pat;
    model(k).H          = H;
    model(k).gamma      = gamma;
    model(k).lamChannel = lamChannel;
    model(k).lamEject   = lamEject;
    model(k).lamBisect  = lamBisection;
    model(k).lamStar    = lamStar;
    model(k).T0         = T0;

    fprintf('%-5s %-8s  H=%5.3f  peak channel load=%5.3f/lambda\n', ...
            topo, pat, H, gamma);
    fprintf('        ideal saturation: channel %5.3f | eject %5.3f | classical bisection %5.3f -> lambda* = %5.3f\n', ...
            lamChannel, lamEject, lamBisection, lamStar);
    fprintf('        zero-load latency T0 = %5.2f cycles\n\n', T0);
end

%% ---- Load the measured sweep -----------------------------------------
csvPath = fullfile(resultsDir, 'week3_sweep_results.csv');
haveMeasured = exist(csvPath, 'file') == 2;
if haveMeasured
    M = readtable(csvPath);
    M = M(strcmp(M.vc_mode, 'baseline_vc2'), :);
else
    warning('bisection_latency_model:noData', ...
            'Measured results not found at %s -- plotting the model only.', csvPath);
end

%% ---- Figure 1: latency vs injection rate, model vs measurement -------
figure(1); clf;
colours = lines(4);
for k = 1:numel(model)
    subplot(2,2,k); hold on; grid on;

    lam = linspace(0.01, 0.98*model(k).lamStar, 300);
    rho = lam / model(k).lamStar;
    T   = model(k).T0 + (rho ./ (1 - rho)) * (L/2);
    plot(lam, T, '-', 'LineWidth', 1.8, 'Color', colours(k,:));

    if haveMeasured
        sel = strcmp(M.topology, model(k).topology) & ...
              strcmp(M.traffic,  model(k).traffic);
        plot(M.offered_rate(sel), M.avg_latency(sel), 'ko', ...
             'MarkerSize', 5, 'MarkerFaceColor', 'w');
    end

    xline(model(k).lamStar, '--', 'ideal \lambda^*');
    set(gca, 'YScale', 'log');
    xlabel('offered load (flits/node/cycle)');
    ylabel('average packet latency (cycles)');
    title(sprintf('%s / %s', model(k).topology, model(k).traffic));
    legend({'analytical model','measured'}, 'Location', 'northwest');
    ylim([1 2e4]);
end
sgtitle('Project 13 / Group 1 -- analytical latency model vs flit-level simulation');

%% ---- Figure 2: ideal vs achieved saturation --------------------------
figure(2); clf;
labels  = arrayfun(@(m) sprintf('%s/%s', m.topology, m.traffic), model, ...
                   'UniformOutput', false);
ideal   = [model.lamStar];
achieved = nan(size(ideal));
if haveMeasured
    for k = 1:numel(model)
        sel = strcmp(M.topology, model(k).topology) & ...
              strcmp(M.traffic,  model(k).traffic) & M.saturated == 0;
        if any(sel), achieved(k) = max(M.offered_rate(sel)); end
    end
end
bar([ideal(:) achieved(:)]); grid on;
set(gca, 'XTickLabel', labels);
ylabel('injection rate (flits/node/cycle)');
legend({'ideal (channel-load model)','achieved (measured, loss-free)'}, ...
       'Location','northwest');
title('Ideal vs achieved saturation injection rate');

%% ---- Write the validation table --------------------------------------
outPath = fullfile(resultsDir, 'week3_model_vs_measured.csv');
fid = fopen(outPath, 'w');
fprintf(fid, ['topology,traffic,avg_hops,peak_channel_load,lam_channel,' ...
              'lam_eject,lam_classical_bisection,lam_star_model,' ...
              'zero_load_latency_model,lam_achieved_measured,efficiency\n']);
for k = 1:numel(model)
    eff = achieved(k) / model(k).lamStar;
    fprintf(fid, '%s,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n', ...
            model(k).topology, model(k).traffic, model(k).H, model(k).gamma, ...
            model(k).lamChannel, model(k).lamEject, model(k).lamBisect, ...
            model(k).lamStar, model(k).T0, achieved(k), eff);
end
fclose(fid);
fprintf('Wrote %s\n', outPath);

end % bisection_latency_model


%% ======================================================================
%  Destination distribution P(dst | src), mirroring the C/Python
%  generators in student_implementation/traffic.c exactly, including the
%  rule that a hot node drawing itself falls through to the uniform
%  component instead of self-addressing.
%  ======================================================================
function P = destinationDistribution(N, pattern, hot, f)
P = zeros(N, N);
for s = 0:N-1
    row = zeros(1, N);
    if strcmp(pattern, 'hotnode')
        fallthrough = 0;
        for h = hot
            if h ~= s
                row(h+1) = row(h+1) + f/numel(hot);
            else
                fallthrough = fallthrough + f/numel(hot);
            end
        end
        remaining = 1 - f + fallthrough;
    else
        remaining = 1;
    end
    for d = 0:N-1
        if d ~= s
            row(d+1) = row(d+1) + remaining/(N-1);
        end
    end
    P(s+1, :) = row;
end
end


%% ======================================================================
%  Walk every source-destination pair through the routing algorithm and
%  accumulate the load each unidirectional channel carries, expressed in
%  units of the per-node injection rate lambda.
%  ======================================================================
function [H, gamma, ejectLoad] = channelLoad(N, rows, cols, topo, P)
load      = containers.Map('KeyType','char','ValueType','double');
ejectLoad = zeros(1, N);
H = 0;

for s = 0:N-1
    for d = 0:N-1
        p = P(s+1, d+1);
        if p <= 0, continue; end
        if strcmp(topo, 'ring')
            path = ringPath(s, d, N);
        else
            path = meshPathXY(s, d, cols);
        end
        H = H + p * (numel(path)-1) / N;
        for i = 1:numel(path)-1
            key = sprintf('%d>%d', path(i), path(i+1));
            if isKey(load, key)
                load(key) = load(key) + p;
            else
                load(key) = p;
            end
        end
        ejectLoad(d+1) = ejectLoad(d+1) + p;
    end
end
gamma = max(cell2mat(values(load)));
end

function path = ringPath(s, d, N)
cw  = mod(d - s, N);
ccw = mod(s - d, N);
if cw <= ccw, step = 1; h = cw; else, step = -1; h = ccw; end
path = zeros(1, h+1); path(1) = s; c = s;
for i = 1:h
    c = mod(c + step, N);
    path(i+1) = c;
end
end

function path = meshPathXY(s, d, cols)
r1 = floor(s/cols); c1 = mod(s, cols);
r2 = floor(d/cols); c2 = mod(d, cols);
path = s; r = r1; c = c1;
while c ~= c2
    if c2 > c, c = c + 1; else, c = c - 1; end
    path(end+1) = r*cols + c;  %#ok<AGROW>
end
while r ~= r2
    if r2 > r, r = r + 1; else, r = r - 1; end
    path(end+1) = r*cols + c;  %#ok<AGROW>
end
end
