function cc_benchmark(matrixFile, varargin)
% CC_BENCH_MATLAB
% Benchmark connected components using MATLAB's conncomp on one graph.
%
% Usage:
%   cc_bench_matlab('graph.mtx')
%   cc_bench_matlab('graph.mat', 'runs', 5)
%
% Params (name/value, no dashes):
%   'runs' : number of repeated timing runs (default: 1)

    %% Parse args
    ip = inputParser;
    addRequired(ip, 'matrixFile', @(s)ischar(s) || isstring(s));
    addParameter(ip, 'runs', 1, @(x)isnumeric(x) && isscalar(x) && x>=1 && isfinite(x));
    parse(ip, matrixFile, varargin{:});
    runs = double(ip.Results.runs);

    %% Load sparse adjacency (memory-minded)
    fprintf('Loading graph: %s\n', matrixFile);
    A = load_sparse_matrix(char(matrixFile));
    if ~issparse(A), A = sparse(A); end
    A = spones(A);                              % structural (0/1)
    % drop self-loops
    n = size(A,1);
    if nnz(diag(A)) > 0
        A = A - spdiags(diag(A), 0, n, n);
    end
    % make undirected 
    if ~isequal(A, A')
        A = (A | A');
    end
    A = logical(A);

    m_undirected = nnz(triu(A));
    fprintf('Graph has %d nodes and ~%d undirected edges\n', n, m_undirected);

    %% Build graph ONCE and reuse
    G = graph(A, 'upper');   % avoids double-storing symmetric edges
    clear A;                 % free adjacency now that G exists

    %% Benchmark
    fprintf('Computing connected components (%d run%s)...\n', runs, plural(runs));

    times = zeros(runs,1);
    labels = [];
    for r = 1:runs
        t0 = tic;
        labels = conncomp(G);
        times(r) = toc(t0);
        fprintf('Run %d time: %.6f seconds\n', r, times(r));
    end

    avgTime = mean(times);
    fprintf('Average time over %d run%s: %.6f seconds.\n', runs, plural(runs), avgTime);

    numComponents = numel(unique(labels));
    fprintf('Number of connected components: %d\n', numComponents);

    %% Output labels (one per line)
    fid = fopen('matlab_labels.txt', 'w');
    if fid < 0, error('Failed to open output file.'); end
    % If you want int32 like C:
    % labels = int32(labels);
    fprintf(fid, '%d\n', labels);
    fclose(fid);
    fprintf('Labels written to matlab_labels.txt\n');
end

%% -------------------- Helpers --------------------

function A = load_sparse_matrix(path)
    [~,~,ext] = fileparts(path);
    switch lower(ext)
        case {'.mtx', '.txt'}
            A = read_mtx_sparse(path);
        case '.mat'
            S = load(path);
            if isfield(S, 'Problem') && isfield(S.Problem, 'A')
                A = S.Problem.A;
            else
                % pick first sparse; else first numeric and convert
                fns = fieldnames(S);
                A = [];
                for k = 1:numel(fns)
                    v = S.(fns{k});
                    if issparse(v), A = v; break; end
                end
                if isempty(A)
                    for k = 1:numel(fns)
                        v = S.(fns{k});
                        if isnumeric(v), A = sparse(v); break; end
                    end
                end
                if isempty(A), error('No matrix found in %s', path); end
            end
        otherwise
            error('Unsupported file extension: %s', ext);
    end
end

function A = read_mtx_sparse(path)
    % Minimal Matrix Market (coordinate) reader; treats values as pattern.
    fid = fopen(path, 'r');
    if fid == -1, error('Failed to open %s', path); end
    c = onCleanup(@() fclose(fid));

    % banner
    first = fgetl(fid);
    if ~ischar(first) || ~startsWith(strtrim(lower(first)), '%%matrixmarket')
        error('Invalid Matrix Market banner.');
    end
    isPattern = contains(lower(first), 'pattern');

    % skip comments
    line = fgetl(fid);
    while ischar(line) && (isempty(line) || startsWith(strtrim(line), '%'))
        line = fgetl(fid);
    end
    if ~ischar(line), error('Unexpected EOF reading size line.'); end

    dims = sscanf(line, '%d %d %d');
    if numel(dims) ~= 3, error('Invalid size line in %s', path); end
    M = dims(1); N = dims(2); nz = dims(3);

    I = zeros(nz,1,'uint32');
    J = zeros(nz,1,'uint32');

    if isPattern
        k = 0;
        while k < nz
            t = fscanf(fid, '%u %u', 2);
            if numel(t) < 2, break; end
            k = k + 1; I(k) = t(1); J(k) = t(2);
        end
    else
        k = 0;
        while k < nz
            t = fscanf(fid, '%u %u %*f', 2);
            if numel(t) < 2, break; end
            k = k + 1; I(k) = t(1); J(k) = t(2);
        end
    end
    if k < nz, I = I(1:k); J = J(1:k); end

    % silently drop out-of-range like your C guard
    nmax = max(M,N);
    mask = (I>=1 & I<=nmax & J>=1 & J<=nmax);
    I = double(I(mask)); J = double(J(mask));

    A = sparse(I, J, true, M, N);  % logical pattern
end

function s = plural(n)
    if n == 1, s = ''; else, s = 's'; end
end
